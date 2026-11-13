import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

CARPETA = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CARPETA.parent.parent / "comun" / "python"))
from aura_sdk import ContextoDePrueba, Mensaje, cargar_conector, procesar

T = datetime(2026, 10, 8, 12, tzinfo=timezone.utc)
H, F = "temp_heladera_c", "temp_freezer_c"


class ContextoConVentanas(ContextoDePrueba):
    def __init__(self, manifiesto):
        super().__init__(manifiesto)
        self.consultas = []

    def serie(self, campo, desde=None, hasta=None):
        if desde is None or hasta is None:
            raise AssertionError("Todas las consultas deben tener ambos límites")
        self.consultas.append((campo, desde, hasta))
        return super().serie(campo, desde, hasta)


class TestConector(unittest.TestCase):
    def setUp(self):
        self.manifiesto, self.conector = cargar_conector(CARPETA)
        self.ctx = ContextoDePrueba(self.manifiesto)
        self.n = 0

    def enviar(self, values, ts=T, ingest_id=None):
        self.n += 1
        msg = Mensaje(hw_id="mac-000000000001", values=values, ts=ts, ingest_id=ingest_id or f"prueba-{self.n}")
        return procesar(self.conector, self.ctx, msg)

    def test_guarda_dos_sondas(self):
        self.assertEqual(self.enviar({H:4.5,F:-18}).resultado,"persistido")
        self.assertEqual(self.ctx.mediciones[0].values,{H:4.5,F:-18})

    def test_intervalos_independientes_no_completan_campo_ausente(self):
        self.enviar({H:4})
        self.enviar({F:-19})
        self.assertEqual(self.ctx.mediciones[1].values,{F:-19})

    def test_tipos_centinelas_y_rangos_invalidos(self):
        for valor in [-127,85,126,-56,float("nan"),float("inf"),10**400,True,"4",None]:
            with self.subTest(valor=valor):
                self.assertEqual(self.enviar({H:valor}).resultado,"descartado")
        self.assertEqual(len(self.ctx.mediciones),0)

    def test_validacion_independiente(self):
        self.enviar({H:85,F:-18,"rssi":-99})
        self.assertEqual(self.ctx.mediciones[0].values,{F:-18})

    def test_valores_fisicos_en_extremos(self):
        self.assertEqual(self.enviar({H:-55,F:125}).resultado,"persistido")

    def test_sin_campos_validos(self):
        for values in [{},{"rssi":-80},{"temp_c":3}]:
            self.assertEqual(self.enviar(values).resultado,"descartado")

    def test_no_inventa_ts_de_medicion(self):
        self.enviar({H:4},ts=None)
        self.assertIsNotNone(self.ctx.mediciones[0].ts)  # llegada asignada por el SDK

    def test_conserva_hora_original(self):
        self.enviar({H:4},ts=T-timedelta(hours=3))
        self.assertEqual(self.ctx.mediciones[0].ts,T-timedelta(hours=3))

    def test_duplicado_no_guarda_ni_alerta_otro(self):
        self.enviar({H:10},ingest_id="igual")
        self.assertEqual(self.enviar({H:10},ingest_id="igual").resultado,"duplicado")
        self.assertEqual(len(self.ctx.mediciones),1)
        self.assertEqual(self.ctx.alertas, [])
        self.enviar({H: 10}, T + timedelta(minutes=1))
        self.assertEqual(len(self.ctx.alertas), 1)

    def test_alarma_una_por_cruce_y_recuperacion(self):
        for n, temp in enumerate([4,8,9,6,4]):
            self.enviar({H:temp},ts=T+timedelta(minutes=n))
        self.assertEqual([a.severidad for a in self.ctx.alertas],["warning","info"])

    def test_alarma_baja_freezer(self):
        self.enviar({F:-26})
        self.enviar({F:-27},T+timedelta(minutes=1))
        self.enviar({F:-18},T+timedelta(minutes=2))
        self.assertEqual([a.tipo for a in self.ctx.alertas],["temperatura_freezer"]*2)

    def test_ambas_alarmas_independientes(self):
        self.enviar({H: 10, F: -10})
        self.enviar({H: 10, F: -10}, T + timedelta(minutes=1))
        self.assertEqual({a.tipo for a in self.ctx.alertas},{"temperatura_heladera","temperatura_freezer"})

    def test_respeta_umbrales_reportados(self):
        self.ctx = ContextoDePrueba(self.manifiesto, config={"hlo":0,"hhi":10})
        self.enviar({H:8})
        self.assertEqual(self.ctx.alertas,[])
        self.enviar({H: 11}, T + timedelta(minutes=1))
        self.assertEqual(self.ctx.alertas, [])
        self.enviar({H: 11}, T + timedelta(minutes=2))
        self.assertEqual(len(self.ctx.alertas), 1)

    def test_config_invalida_usa_defaults(self):
        self.ctx = ContextoDePrueba(self.manifiesto, config={"hlo":9,"hhi":2})
        self.enviar({H: 8})
        self.enviar({H: 8}, T + timedelta(minutes=1))
        self.assertEqual(len(self.ctx.alertas), 1)

    def test_recrear_conector_no_repite_alarma(self):
        self.enviar({H: 8})
        _, self.conector = cargar_conector(CARPETA)
        self.enviar({H: 9}, T + timedelta(minutes=1))
        self.assertEqual(len(self.ctx.alertas), 1)
        _, self.conector = cargar_conector(CARPETA)
        self.enviar({H: 6}, T + timedelta(minutes=2))
        self.enviar({H: 7}, T + timedelta(minutes=3))
        self.assertEqual(len(self.ctx.alertas), 1)

    def test_dato_antiguo_no_simula_recuperacion(self):
        self.enviar({H: 8}, T)
        self.enviar({H: 8}, T + timedelta(minutes=1))
        self.enviar({H: 4}, T - timedelta(hours=1))
        self.assertEqual(len(self.ctx.mediciones), 3)
        self.assertEqual(len(self.ctx.alertas), 1)

    def test_dato_antiguo_no_simula_alarma_actual(self):
        self.enviar({H:4},T)
        self.enviar({H:8},T-timedelta(hours=1))
        self.assertEqual(self.ctx.alertas,[])

    def test_cambio_de_baja_a_alta(self):
        for n, temperatura in enumerate([0, 0, 10, 10]):
            self.enviar({H: temperatura}, T + timedelta(minutes=n))
        self.assertEqual([a.severidad for a in self.ctx.alertas], ["warning", "warning"])

    def test_limites_de_conservacion_inclusivos(self):
        self.enviar({H:2,F:-25})
        self.enviar({H:6,F:-15},T+timedelta(minutes=1))
        self.assertEqual(self.ctx.alertas,[])

    def test_oscilacion_y_recuperacion_con_histeresis(self):
        for n, temperatura in enumerate([6, 6.5] * 5):
            self.enviar({H: temperatura}, T + timedelta(minutes=n))
        self.assertEqual(self.ctx.alertas, [])

        for n, temperatura in enumerate([6, 6.5, 6.5, 6, 6.5, 5.5, 6.5], start=10):
            self.enviar({H: temperatura}, T + timedelta(minutes=n))
        self.assertEqual([a.severidad for a in self.ctx.alertas], ["warning", "info"])
        self.assertIn((T + timedelta(minutes=12)).isoformat(), self.ctx.alertas[0].mensaje)
        self.assertIn((T + timedelta(minutes=15)).isoformat(), self.ctx.alertas[1].mensaje)

    def test_histeresis_inferior_y_rango_estrecho(self):
        self.ctx = ContextoDePrueba(self.manifiesto, config={"hlo": 2, "hhi": 2.4})
        for n, temperatura in enumerate([1.5, 1.5, 2, 2.1, 2.2]):
            self.enviar({H: temperatura}, T + timedelta(minutes=n))
        self.assertEqual([a.severidad for a in self.ctx.alertas], ["warning", "info"])
        self.assertIn((T + timedelta(minutes=4)).isoformat(), self.ctx.alertas[1].mensaje)

    def test_otra_sonda_no_cuenta_para_confirmar(self):
        self.enviar({H: 8})
        self.enviar({F: -10}, T + timedelta(minutes=1))
        self.enviar({H: 85, F: -18}, T + timedelta(minutes=2))
        self.assertEqual(self.ctx.alertas, [])
        self.enviar({H: 9}, T + timedelta(minutes=3))
        self.assertEqual([a.tipo for a in self.ctx.alertas], ["temperatura_heladera"])

    def test_camino_normal_usa_una_sola_ventana(self):
        self.ctx = ContextoConVentanas(self.manifiesto)
        ahora = datetime.now(timezone.utc)
        self.enviar({H: 4}, ahora - timedelta(minutes=1))
        self.ctx.consultas.clear()

        self.enviar({H: 4.5}, ahora)

        self.assertEqual(self.ctx.consultas, [(H, ahora - timedelta(days=1), ahora + timedelta(days=1))])

    def test_alarma_de_hace_seis_dias_se_recupera(self):
        self.ctx = ContextoConVentanas(self.manifiesto)
        ahora = datetime.now(timezone.utc)
        for ts, temperatura in [
            (ahora - timedelta(days=6, minutes=1), 8),
            (ahora - timedelta(days=6), 8),
            (ahora - timedelta(days=2), 6),
            (ahora - timedelta(minutes=1), 6),
        ]:
            self.enviar({H: temperatura}, ts)
        self.ctx.consultas.clear()

        self.enviar({H: 5.5}, ahora)

        self.assertEqual([a.severidad for a in self.ctx.alertas], ["warning", "info"])
        self.assertEqual(
            [desde for _, desde, _ in self.ctx.consultas],
            [ahora - timedelta(days=dias) for dias in (1, 3, 7)],
        )

    def test_lectura_en_borde_de_ventana_no_cuenta_dos_veces(self):
        self.ctx = ContextoConVentanas(self.manifiesto)
        ahora = datetime.now(timezone.utc)
        self.enviar({H: 8}, ahora - timedelta(days=1))

        self.enviar({H: 4}, ahora)

        self.assertEqual(self.ctx.alertas, [])

    def test_dato_atrasado_varios_dias_detecta_muestra_reciente(self):
        self.ctx = ContextoConVentanas(self.manifiesto)
        ahora = datetime.now(timezone.utc)
        self.enviar({H: 8}, ahora - timedelta(days=3, minutes=1))
        self.enviar({H: 4}, ahora - timedelta(minutes=1))
        self.ctx.consultas.clear()

        self.enviar({H: 8}, ahora - timedelta(days=3))

        self.assertEqual(len(self.ctx.mediciones), 3)
        self.assertEqual(self.ctx.alertas, [])
        self.assertEqual(len(self.ctx.consultas), 2)

    def test_no_prediccion_opcional(self):
        self.assertIsNone(self.manifiesto.prediccion)


if __name__ == "__main__":
    unittest.main()
