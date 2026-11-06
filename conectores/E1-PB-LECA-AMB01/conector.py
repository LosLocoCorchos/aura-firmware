"""Conector de E1-PB-LECA-AMB01 — nodo de gas y CO2 del LabECA.

Guarda lo que llega (co2_ppm, temp_c, hum_pct, gas_mv) y alerta cuando el CO2 o el gas pasan su
umbral: una alerta al cruzarlo hacia arriba y otra, informativa, al volver. Sin predicción.

Las alertas por umbral las calcula AURA a partir de `values` (contrato §3.5); el buzzer local del
nodo es independiente. Los umbrales son los de GAS_ALARMA_MV y CO2_ALARMA_PPM del firmware
(dispositivos/E1-PB-LECA-AMB01/main/alarma.h): si se ajustan allá tras calibrar, ajustarlos acá.

No usa atributos de la instancia para recordar nada: AURA puede recrearla en cualquier momento.
Para saber si ya estaba en alarma compara con la última medición guardada (ctx.serie).
"""
from aura_sdk import Conector

UMBRAL_CO2_PPM = 1500
UMBRAL_GAS_MV = 2400

# campo, tipo de alerta, umbral, severidad al cruzar, cómo se nombra en el mensaje
ALERTAS = (
    ("co2_ppm", "dioxido_carbono_alto", UMBRAL_CO2_PPM, "warning", "CO2", "ppm"),
    ("gas_mv", "gas_alto", UMBRAL_GAS_MV, "high", "Gas (MQ2)", "mV"),
)


class ConectorAmbiente(Conector):
    def al_recibir_datos(self, msg, ctx):
        # La última guardada de cada campo, antes de guardar esta muestra.
        anteriores = {campo: ctx.serie(campo)[-1:] for campo, *_ in ALERTAS}

        guardado = ctx.guardar_medicion(msg.values, msg.ts)   # quita lo fuera de rango
        if not guardado:
            return   # no quedó nada válido: AURA la descarta (sin_campos_validos)

        # Se alerta con lo guardado, no con msg.values: un valor fuera de rango no cuenta.
        for campo, tipo, umbral, severidad, nombre, unidad in ALERTAS:
            if campo not in guardado:
                continue
            valor = guardado[campo]
            anterior = anteriores[campo]
            estaba_alto = bool(anterior) and anterior[0][1] >= umbral
            if valor >= umbral and not estaba_alto:
                ctx.alertar(tipo, severidad, f"{nombre}: {valor} {unidad} (umbral {umbral} {unidad})")
            elif valor < umbral and estaba_alto:
                ctx.alertar(tipo, "info", f"{nombre} volvió a {valor} {unidad}")
