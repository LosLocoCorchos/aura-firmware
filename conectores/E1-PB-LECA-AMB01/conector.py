"""Conector de E1-PB-LECA-AMB01 — nodo de gas y CO2 del LabECA.

Guarda lo que llega (co2_ppm, temp_c, hum_pct, gas_mv). Sin predicción y sin alertas por umbral.

Las alertas de CO2 alto y gas alto las emite el firmware del nodo (`dioxido_carbono_alto` y
`gas_alto`, en `hw/<hw_id>/alerts/<tipo>`) en el instante en que su alarma local se activa o se
normaliza. El nodo solo manda una muestra cada `intervalo_s` (puede ser de minutos u horas) y la
alarma evalúa cada 5 s, así que calcular el umbral acá a partir de `values` llegaría tarde o no
vería el pico. Además así los umbrales viven en un solo lugar (`alarma.h` del firmware) y no hay
que mantenerlos sincronizados con este archivo.
"""
from aura_sdk import Conector


class ConectorAmbiente(Conector):
    def al_recibir_datos(self, msg, ctx):
        guardado = ctx.guardar_medicion(msg.values, msg.ts)   # quita lo fuera de rango
        if not guardado:
            return   # no quedó nada válido: AURA la descarta (sin_campos_validos)
