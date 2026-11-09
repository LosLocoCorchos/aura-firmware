"""Tests del conector del medidor del LabECA, sin AURA ni placa. Correr desde la raíz del repo:

    python3 -m unittest discover -s conectores/E1-PB-LECA-TAB01/tests
"""
import json
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

CARPETA = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CARPETA.parent.parent / "comun" / "python"))   # aura_sdk
from aura_sdk import ContextoDePrueba, Mensaje, cargar_conector, procesar  # noqa: E402

T0 = datetime(2026, 10, 7, 12, 0, tzinfo=timezone.utc)
HW = "mac-e072a1f7efe4"

# Mediciones reales del tablero del LabECA, de las pruebas de OTA del 30/10 y del 6/11 de 2026 (una de
# cada tres). El 30/10 el banco no tenía hora: esas llevan la de llegada al broker.
DATOS_REALES = Path(__file__).resolve().parent / "tablero_real.jsonl"

# Una medición normal del aula: 232,85 V, 12,3 A, 2,7 kW, cos φ 0,97.
NORMAL = {"v_an_v": 232.85, "i_a_a": 12.34, "p_w": 2723.5, "cosphi_a": 0.97}


def msg(values, n, ts=T0, cada=timedelta(seconds=10)):
    """La n-ésima muestra, una cada 10 s como manda el firmware."""
    return Mensaje(hw_id=HW, values=values, ingest_id=f"id-{n}", ts=ts + n * cada if ts else None)


class Base(unittest.TestCase):
    def setUp(self):
        self.manifiesto, self.conector = cargar_conector(CARPETA)
        self.ctx = ContextoDePrueba(self.manifiesto)

    def serie_de(self, valores_por_muestra):
        """Procesa una lista de dicts de values y devuelve las alertas como (tipo, severidad)."""
        for n, values in enumerate(valores_por_muestra):
            procesar(self.conector, self.ctx, msg(values, n))
        return [(a.tipo, a.severidad) for a in self.ctx.alertas]


class Manifiesto(Base):
    def test_declara_exactamente_lo_que_manda_el_firmware(self):
        # Si cambia el firmware, este test obliga a revisar el manifiesto (y al revés).
        self.assertEqual(set(self.manifiesto.campos), {
            "v_an_v", "i_a_a", "p_w", "cosphi_a",                      # obligatorios
            "tanphi", "thd_v_an_pct", "thd_i_a_pct", "e_act_kwh",      # opcionales
        })


class Guardar(Base):
    def test_guarda_una_medicion_normal(self):
        r = procesar(self.conector, self.ctx, msg(NORMAL, 0))
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.mediciones[0].values, NORMAL)
        self.assertEqual(self.ctx.alertas, [])

    def test_guarda_los_opcionales(self):
        values = dict(NORMAL, tanphi=-0.61, thd_v_an_pct=1.99, thd_i_a_pct=40.99, e_act_kwh=6564.06)
        r = procesar(self.conector, self.ctx, msg(values, 0))
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.mediciones[0].values, values)

    def test_un_campo_fuera_de_rango_se_quita_y_el_resto_se_guarda(self):
        r = procesar(self.conector, self.ctx, msg(dict(NORMAL, cosphi_a=1.5), 0))
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(r.quitados, ["cosphi_a"])
        self.assertNotIn("cosphi_a", self.ctx.mediciones[0].values)

    def test_un_campo_no_declarado_no_se_guarda(self):
        r = procesar(self.conector, self.ctx, msg(dict(NORMAL, rssi=-70), 0))
        self.assertEqual(r.quitados, ["rssi"])

    def test_sin_campos_validos_se_descarta(self):
        r = procesar(self.conector, self.ctx, msg({"v_an_v": 999, "rssi": -70}, 0))
        self.assertEqual((r.resultado, r.motivo), ("descartado", "sin_campos_validos"))
        self.assertEqual(self.ctx.alertas, [])

    def test_un_reintento_sale_como_duplicado(self):
        procesar(self.conector, self.ctx, msg(NORMAL, 0))
        r = procesar(self.conector, self.ctx, msg(NORMAL, 0))   # mismo ingest_id
        self.assertEqual(r.resultado, "duplicado")
        self.assertEqual(len(self.ctx.mediciones), 1)

    def test_sin_hora_del_nodo_se_guarda_con_la_de_llegada(self):
        r = procesar(self.conector, self.ctx, msg(NORMAL, 0, ts=None))
        self.assertEqual(r.resultado, "persistido")
        self.assertIsNotNone(self.ctx.mediciones[0].ts)


class Alertas(Base):
    def test_subtension_una_vez_por_cruce(self):
        tensiones = [221, 195, 190, 196, 221, 222]
        alertas = self.serie_de([dict(NORMAL, v_an_v=v) for v in tensiones])
        self.assertEqual(alertas, [("subtension", "warning"), ("subtension", "info")])

    def test_sobretension_una_vez_por_cruce(self):
        tensiones = [230, 245, 250, 236]
        alertas = self.serie_de([dict(NORMAL, v_an_v=v) for v in tensiones])
        self.assertEqual(alertas, [("sobretension", "warning"), ("sobretension", "info")])

    def test_sobrecorriente(self):
        umbral = sys.modules[type(self.conector).__module__].I_ALERTA_A   # sale de TERMICA_A
        corrientes = [umbral - 5, umbral + 1, umbral + 2, umbral - 3]
        alertas = self.serie_de([dict(NORMAL, i_a_a=i) for i in corrientes])
        self.assertEqual(alertas, [("sobrecorriente", "high"), ("sobrecorriente", "info")])

    def test_potencia_negativa_avisa_del_ti_invertido(self):
        potencias = [140, -138, -140, 139]
        alertas = self.serie_de([dict(NORMAL, p_w=p) for p in potencias])
        self.assertEqual(alertas, [("potencia_negativa", "warning"), ("potencia_negativa", "info")])
        self.assertIn("TI", self.ctx.alertas[0].mensaje)

    def test_ruido_cerca_de_cero_no_es_potencia_negativa(self):
        alertas = self.serie_de([dict(NORMAL, p_w=p) for p in [5, -3, -20, 4]])
        self.assertEqual(alertas, [])

    def test_bajo_cosphi_solo_con_carga(self):
        muestras = [
            dict(NORMAL, cosphi_a=0.60, p_w=50),     # poca carga: el ángulo no es confiable
            dict(NORMAL, cosphi_a=0.60, p_w=500),    # con carga: alerta
            dict(NORMAL, cosphi_a=0.62, p_w=520),    # sigue igual: nada
            dict(NORMAL, cosphi_a=0.95, p_w=520),    # se normaliza
        ]
        self.assertEqual(self.serie_de(muestras), [("bajo_cosphi", "warning"), ("bajo_cosphi", "info")])

    def test_bajo_cosphi_con_poca_carga_no_se_normaliza(self):
        # Revisión del PR #11: con menos de 200 W la regla daba "Ya no hay bajo cos φ" aunque el
        # cos φ siguiera bajo. Ahora con poca carga no dice nada, y al volver la carga tampoco
        # repite la alerta: sigue la misma hasta que el cos φ se normalice.
        muestras = [
            dict(NORMAL, cosphi_a=0.60, p_w=500),    # con carga: alerta
            dict(NORMAL, cosphi_a=0.60, p_w=50),     # poca carga: no se sabe, nada
            dict(NORMAL, cosphi_a=0.61, p_w=500),    # vuelve la carga, sigue bajo: nada nuevo
            dict(NORMAL, cosphi_a=0.95, p_w=500),    # se normaliza
        ]
        self.assertEqual(self.serie_de(muestras), [("bajo_cosphi", "warning"), ("bajo_cosphi", "info")])

    def test_bajo_cosphi_se_normaliza_cuando_vuelve_la_carga_con_buen_cosphi(self):
        muestras = [
            dict(NORMAL, cosphi_a=0.60, p_w=500),    # alerta
            dict(NORMAL, cosphi_a=0.95, p_w=50),     # poca carga: aunque el cos φ sea bueno, no se sabe
            dict(NORMAL, cosphi_a=0.96, p_w=500),    # con carga y cos φ bueno: se normaliza
        ]
        self.assertEqual(self.serie_de(muestras), [("bajo_cosphi", "warning"), ("bajo_cosphi", "info")])
        self.assertIn("0.96", self.ctx.alertas[1].mensaje)

    def test_un_campo_que_falta_no_normaliza_la_alerta(self):
        muestras = [
            dict(NORMAL, v_an_v=190),                                 # subtensión
            {k: v for k, v in NORMAL.items() if k != "v_an_v"},       # sin tensión: no se sabe
            dict(NORMAL, v_an_v=191),                                 # sigue baja: nada nuevo
        ]
        self.assertEqual(self.serie_de(muestras), [("subtension", "warning")])

    def test_un_valor_fuera_de_rango_no_dispara_alertas(self):
        # 999 V no es una sobretensión: es una lectura imposible, y AURA la quita.
        self.assertEqual(self.serie_de([dict(NORMAL, v_an_v=999)]), [])

    def test_despues_de_un_hueco_largo_vuelve_a_avisar_si_sigue_el_problema(self):
        procesar(self.conector, self.ctx, msg(dict(NORMAL, v_an_v=190), 0))
        tarde = Mensaje(hw_id=HW, values=dict(NORMAL, v_an_v=190), ingest_id="id-tarde",
                        ts=T0 + timedelta(hours=2))
        procesar(self.conector, self.ctx, tarde)
        self.assertEqual([(a.tipo, a.severidad) for a in self.ctx.alertas],
                         [("subtension", "warning"), ("subtension", "warning")])


class DatosReales(Base):
    """Con el tablero funcionando normal no tiene que saltar ninguna alerta. Si un cambio de umbral o
    de regla las hace saltar con estos datos, este test lo avisa antes de que pase en AURA."""

    def setUp(self):
        super().setUp()
        lineas = DATOS_REALES.read_text(encoding="utf-8").splitlines()
        self.muestras = [json.loads(linea) for linea in lineas if linea.strip()]

    def test_hay_datos_de_las_dos_pruebas(self):
        dias = {m["ts"][:10] for m in self.muestras}
        self.assertEqual(dias, {"2026-10-30", "2026-11-06"})
        self.assertGreater(len(self.muestras), 100)

    def test_con_el_tablero_normal_se_guarda_todo_sin_alertas(self):
        for m in self.muestras:
            ts = datetime.fromisoformat(m["ts"].replace("Z", "+00:00"))
            r = procesar(self.conector, self.ctx,
                         Mensaje(hw_id=HW, values=m["values"], ingest_id=m["ingest_id"], ts=ts))
            self.assertEqual(r.resultado, "persistido", m)
        self.assertEqual(len(self.ctx.mediciones), len(self.muestras))
        self.assertEqual(self.ctx.alertas, [])


if __name__ == "__main__":
    unittest.main()
