"""Tests del conector de E2-AZ-TERR-TAN01. Correr desde la raíz del repo:

    python -m unittest discover -s conectores/E2-AZ-TERR-TAN01/tests
"""
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

CARPETA = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CARPETA.parent.parent / "comun" / "python"))   # aura_sdk
from aura_sdk import ContextoDePrueba, Mensaje, cargar_conector, procesar  # noqa: E402

T0 = datetime(2026, 10, 9, 12, 0, tzinfo=timezone.utc)
SIN_ECO = 65535   # 0xFFFF: un valor fuera de rango, como el que el firmware ya no manda


def msg(values, segundos=0):
    return Mensaje(hw_id="eui-0000000000000001", values=values, ts=T0 + timedelta(seconds=segundos),
                   ingest_id=f"id-{segundos}")


def tanque(n, dist, nivel=None):
    v = {f"distancia_tanque{n}_cm": dist}
    if nivel is not None:
        v[f"nivel_tanque{n}_pct"] = nivel
    return v


class ConectorTanques(unittest.TestCase):
    def setUp(self):
        self.manifiesto, self.conector = cargar_conector(CARPETA)
        self.ctx = ContextoDePrueba(self.manifiesto)
        self.t = 0

    def alertas(self, tipo):
        return [a for a in self.ctx.alertas if a.tipo == tipo]

    def enviar(self, values, paso=60):
        """Un mensaje cada `paso` segundos, como los ciclos del nodo con intervalo_s = 60."""
        r = procesar(self.conector, self.ctx, msg(values, self.t))
        self.t += paso
        return r

    def ciclos(self, n, d1=150, d2=200, d3=300, niveles=(None, None, None), sin=(), paso=60):
        """n ciclos normales: un mensaje con los tres tanques, salvo los de `sin`."""
        for _ in range(n):
            valores = {}
            for numero, d in ((1, d1), (2, d2), (3, d3)):
                if numero not in sin:
                    valores.update(tanque(numero, d, niveles[numero - 1]))
            self.enviar(valores, paso)

    # --- guardar ---

    def test_un_ciclo_guarda_los_tres(self):
        r = self.enviar({**tanque(1, 245.3), **tanque(2, 243), **tanque(3, 244.3)})
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.serie("distancia_tanque2_cm"), [(T0, 243)])

    def test_guarda_los_tres_con_nivel(self):
        valores = {**tanque(1, 150, 50.0), **tanque(2, 200, 30.5), **tanque(3, 25, 100.0)}
        r = self.enviar(valores)
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.serie("nivel_tanque2_pct"), [(T0, 30.5)])

    def test_fuera_de_rango_no_se_guarda(self):
        for valores in (tanque(1, 24.9), tanque(1, 600.1), tanque(1, SIN_ECO), tanque(1, 100, 100.1)):
            r = self.enviar(valores)
            if "nivel_tanque1_pct" in valores:
                self.assertEqual(r.quitados, ["nivel_tanque1_pct"])
            else:
                self.assertEqual((r.resultado, r.motivo), ("descartado", "sin_campos_validos"))

    def test_los_limites_del_rango_si_se_guardan(self):
        for valores in (tanque(1, 25, 0), tanque(1, 600, 100)):
            self.assertEqual(self.enviar(valores).resultado, "persistido")

    def test_un_campo_que_no_esta_en_el_manifiesto_no_se_guarda(self):
        r = self.enviar({"distancia_tanque1_mm": 1500})
        self.assertEqual((r.resultado, r.motivo), ("descartado", "sin_campos_validos"))

    # --- tanque_bajo, con el nivel ---

    def test_alerta_una_vez_por_cruce(self):
        for nivel in (50, 15, 12, 10, 40, 60):
            self.enviar(tanque(1, 100, nivel))
        self.assertEqual([a.severidad for a in self.alertas("tanque_bajo")], ["warning", "info"])
        self.assertIn("Tanque 1", self.alertas("tanque_bajo")[0].mensaje)

    def test_histeresis_no_repite_avisos_con_el_nivel_en_el_borde(self):
        for nivel in (50, 19, 21, 19.5, 22, 20, 24.9):
            self.enviar(tanque(1, 100, nivel))
        self.assertEqual([a.severidad for a in self.alertas("tanque_bajo")], ["warning"])
        self.enviar(tanque(1, 100, 25))   # mínimo + histéresis
        self.assertEqual([a.severidad for a in self.alertas("tanque_bajo")], ["warning", "info"])

    def test_horas_en_la_banda_de_histeresis_no_duplican_el_aviso(self):
        # El caso de la revisión: 50, 15, más de 2 h en 22 (entre el mínimo y mínimo + histéresis)
        # y después 19. Antes daba [warning, warning]; tiene que ser un solo warning.
        # Llegan los tres tanques, como en el nodo (así la ventana corta de 2 h alcanza).
        otros = {**tanque(2, 100, 80), **tanque(3, 100, 80)}
        self.enviar({**tanque(1, 100, 50), **otros})
        self.enviar({**tanque(1, 100, 15), **otros})
        for _ in range(125):
            self.enviar({**tanque(1, 100, 22), **otros})
        self.enviar({**tanque(1, 100, 19), **otros})
        self.assertEqual([a.severidad for a in self.alertas("tanque_bajo")], ["warning"])
        self.enviar({**tanque(1, 100, 30), **otros})
        self.assertEqual([a.severidad for a in self.alertas("tanque_bajo")], ["warning", "info"])

    def test_cada_tanque_alerta_por_separado(self):
        self.enviar({**tanque(1, 100, 10), **tanque(2, 100, 80), **tanque(3, 100, 50)})
        self.assertEqual([a.mensaje[:9] for a in self.alertas("tanque_bajo")], ["Tanque 1:"])

    def test_sin_nivel_no_hay_tanque_bajo(self):
        for d in (100, 300, 590):
            self.enviar(tanque(1, d))
        self.assertEqual(self.alertas("tanque_bajo"), [])

    def test_tanque_lleno_no_es_falla_ni_tanque_bajo(self):
        self.ciclos(4, d1=25, d2=25, d3=25, niveles=(100, 100, 100))
        self.assertEqual(self.alertas("sensor"), [])
        self.assertEqual(self.alertas("tanque_bajo"), [])

    # --- sensor: un tanque deja de llegar mientras los otros siguen ---

    def test_ciclos_normales_no_alertan(self):
        self.ciclos(10)
        self.assertEqual(self.alertas("sensor"), [])

    def test_un_sensor_que_deja_de_medir_alerta_una_vez_y_avisa_cuando_vuelve(self):
        self.ciclos(2)
        self.ciclos(2, sin=(2,))
        self.assertEqual(self.alertas("sensor"), [])   # 2 ciclos: todavía no
        self.ciclos(4, sin=(2,))
        alertas = [(a.severidad, a.mensaje[:9]) for a in self.alertas("sensor")]
        self.assertEqual(alertas, [("warning", "Tanque 2:")])
        self.ciclos(2)
        alertas = [(a.severidad, a.mensaje[:9]) for a in self.alertas("sensor")]
        self.assertEqual(alertas, [("warning", "Tanque 2:"), ("info", "Tanque 2:")])

    def test_una_medicion_perdida_no_alerta(self):
        self.ciclos(2)
        self.ciclos(1, sin=(3,))
        self.ciclos(3)
        self.assertEqual(self.alertas("sensor"), [])

    def test_un_sensor_que_nunca_midio_alerta_una_sola_vez(self):
        self.ciclos(8, sin=(3,))
        self.assertEqual([a.severidad for a in self.alertas("sensor")], ["warning"])
        self.assertIn("Tanque 3", self.alertas("sensor")[0].mensaje)

    def test_no_depende_del_intervalo(self):
        # Con intervalo_s = 12 h los avisos tienen que ser los mismos que con 60 s.
        paso = 12 * 3600
        self.ciclos(2, paso=paso)
        self.ciclos(4, sin=(2,), paso=paso)
        self.assertEqual([a.severidad for a in self.alertas("sensor")], ["warning"])
        self.ciclos(1, paso=paso)
        self.assertEqual([a.severidad for a in self.alertas("sensor")], ["warning", "info"])

    def test_si_fallan_los_tres_lo_avisa_el_codec_no_el_conector(self):
        # Si fallan los tres, el nodo manda un mensaje de estado (máscara 0) y el codec lo
        # traduce a errors: el bridge no publica data y al conector no llega nada. Un mensaje
        # vacío (que AURA descarta antes) tampoco tiene que disparar alertas acá.
        self.ciclos(2)
        r = self.enviar({})
        self.assertEqual(r.resultado, "descartado")
        self.assertEqual(self.alertas("sensor"), [])


if __name__ == "__main__":
    unittest.main()
