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

    def test_no_alerta_por_umbral(self):
        # Las alertas de CO2 y de humo/vapores las emite el firmware (alerts/dioxido_carbono_alto, alerts/humo_vapores)
        # cuando su alarma cambia. Si el conector también alertara, AURA las vería duplicadas.
        for i, (co2, gas) in enumerate([(600, 900), (2200, 3000), (700, 1000)]):
            self.procesar(completo(i, co2=co2, gas=gas))
        self.assertEqual(self.ctx.alertas, [])
        self.assertEqual(self.ctx.serie("co2_ppm"), [(T0, 600), (T0 + timedelta(minutes=1), 2200), (T0 + timedelta(minutes=2), 700)])

    def test_un_valor_fuera_de_rango_no_se_guarda_ni_alerta(self):
        # 60000 ppm supera el rango del SCD40: no es una medición.
        self.procesar(msg(0, co2_ppm=60000, temp_c=22.0))
        self.assertEqual(self.ctx.serie("co2_ppm"), [])
        self.assertEqual(self.ctx.alertas, [])

    def test_gas_por_encima_de_lo_que_publica_el_firmware_se_quita(self):
        # El ADC de la C3 satura a ~4083 mV de AO: arriba de eso el firmware no publica gas_mv.
        self.procesar(msg(0, co2_ppm=600, gas_mv=4500))
        self.assertEqual(self.ctx.serie("gas_mv"), [])
        self.procesar(msg(1, co2_ppm=600, gas_mv=4082))
        self.assertEqual(self.ctx.serie("gas_mv"), [(T0 + timedelta(minutes=1), 4082)])


if __name__ == "__main__":
    unittest.main()
