"""Tests del conector, sin AURA ni placa. Correr desde la raíz del repo:

    python3 -m unittest discover -s conectores/E1-PB-LECA-AMB01/tests
"""
import sys
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

CARPETA = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(CARPETA.parent.parent / "comun" / "python"))   # aura_sdk
from aura_sdk import ContextoDePrueba, Mensaje, cargar_conector, procesar  # noqa: E402

T0 = datetime(2026, 10, 9, 12, 0, tzinfo=timezone.utc)
HW = "mac-e072a1f7efe4"


def msg(minuto, **values):
    return Mensaje(hw_id=HW, values=values, ts=T0 + timedelta(minutes=minuto), ingest_id=f"id-{minuto}")


def completo(minuto, co2=600, gas=900):
    return msg(minuto, co2_ppm=co2, temp_c=22.5, hum_pct=45.0, gas_mv=gas)


def alertas(ctx):
    return [(a.tipo, a.severidad) for a in ctx.alertas]


class ConectorAmbiente(unittest.TestCase):
    def setUp(self):
        self.manifiesto, self.conector = cargar_conector(CARPETA)
        self.ctx = ContextoDePrueba(self.manifiesto)

    def procesar(self, m):
        return procesar(self.conector, self.ctx, m)

    def test_guarda_las_cuatro_variables(self):
        r = self.procesar(completo(0))
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.serie("co2_ppm"), [(T0, 600)])
        self.assertEqual(self.ctx.serie("temp_c"), [(T0, 22.5)])
        self.assertEqual(self.ctx.serie("hum_pct"), [(T0, 45.0)])
        self.assertEqual(self.ctx.serie("gas_mv"), [(T0, 900)])

    def test_sin_el_mq2_guarda_lo_del_scd40(self):
        # El firmware no manda gas_mv si el MQ2 falla: la muestra es válida igual.
        r = self.procesar(msg(0, co2_ppm=600, temp_c=22.5, hum_pct=45.0))
        self.assertEqual(r.resultado, "persistido")
        self.assertEqual(self.ctx.serie("gas_mv"), [])

    def test_valor_fuera_de_rango_se_quita_y_el_resto_se_guarda(self):
        self.procesar(msg(0, co2_ppm=600, gas_mv=9000))
        self.assertEqual(self.ctx.serie("co2_ppm"), [(T0, 600)])
        self.assertEqual(self.ctx.serie("gas_mv"), [])

    def test_campo_no_declarado_se_quita(self):
        self.procesar(msg(0, co2_ppm=600, temp_chip_c=40))
        self.assertEqual(self.ctx.mediciones[0].values, {"co2_ppm": 600})

    def test_todo_invalido_se_descarta(self):
        r = self.procesar(msg(0, co2_ppm=-5, gas_mv=99999))
        self.assertEqual((r.resultado, r.motivo), ("descartado", "sin_campos_validos"))
        self.assertEqual(self.ctx.alertas, [])

    def test_un_valor_fuera_de_rango_no_dispara_alerta(self):
        # 60000 ppm supera el rango del SCD40: no es una medición, no es una alarma.
        self.procesar(msg(0, co2_ppm=60000, temp_c=22.0))
        self.assertEqual(self.ctx.alertas, [])

    def test_co2_alerta_una_vez_por_cruce(self):
        for i, co2 in enumerate([600, 1400, 1500, 1800, 2200, 900, 800]):
            self.procesar(completo(i, co2=co2))
        self.assertEqual(alertas(self.ctx), [("dioxido_carbono_alto", "warning"), ("dioxido_carbono_alto", "info")])

    def test_gas_alerta_una_vez_por_cruce(self):
        for i, gas in enumerate([900, 2399, 2400, 3000, 1200]):
            self.procesar(completo(i, gas=gas))
        self.assertEqual(alertas(self.ctx), [("gas_alto", "high"), ("gas_alto", "info")])

    def test_el_umbral_es_inclusivo_como_en_el_firmware(self):
        self.procesar(completo(0, co2=1500))
        self.assertEqual(alertas(self.ctx), [("dioxido_carbono_alto", "warning")])

    def test_primera_muestra_ya_alta_alerta(self):
        self.procesar(completo(0, gas=3500))
        self.assertEqual(alertas(self.ctx), [("gas_alto", "high")])

    def test_co2_y_gas_son_independientes(self):
        self.procesar(completo(0, co2=1600, gas=900))
        self.procesar(completo(1, co2=1600, gas=2600))
        self.assertEqual(alertas(self.ctx), [("dioxido_carbono_alto", "warning"), ("gas_alto", "high")])

    def test_sin_el_campo_no_se_alerta_ni_se_da_por_recuperado(self):
        self.procesar(completo(0, gas=3000))
        self.procesar(msg(1, co2_ppm=600, temp_c=22.5, hum_pct=45.0))   # el MQ2 falló
        self.procesar(completo(2, gas=3100))                            # sigue alto
        self.assertEqual(alertas(self.ctx), [("gas_alto", "high")])


if __name__ == "__main__":
    unittest.main()
