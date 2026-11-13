"""Conector de E1-PB-LECA-CDC01 — CO2 del laboratorio (MH-Z19C sobre ESP-WIFI-MESH).

La placa manda solo co2_ppm. El conector lo guarda y avisa cuando la calidad del aire cambia
de nivel, una vez por cambio y no en cada medición. Usa un solo tipo de alerta, aire_calidad,
y la severidad dice en qué nivel está el aire:

    verde     hasta ATENCION_PPM                    info: el aire volvió a lo normal
    amarillo  de ATENCION_PPM a ALTO_PPM            warning: conviene ventilar
    rojo      más de ALTO_PPM                       high: aire de mala calidad

Como todos los avisos son del mismo tipo, cualquier recorrido de niveles queda coherente
(verde, amarillo, rojo y de vuelta): al volver a verde siempre llega el info.

Los umbrales son los mismos que usan los LEDs de la placa. Si alguien los cambia con set_config
(atencion_ppm, alto_ppm), el conector usa los que reportó la placa (ctx.config_vigente()); si no
los hay o no son válidos, usa los de abajo.

No usa atributos de la instancia para recordar nada: AURA puede recrearla en cualquier
momento. Para saber en qué nivel estaba, mira la última medición guardada con ctx.serie().
"""
from aura_sdk import Conector

TIPO_ALERTA = "aire_calidad"   # un solo tipo: la severidad dice el nivel
ATENCION_PPM = 1000   # por encima, el aire merece ventilación (LED amarillo)
ALTO_PPM = 1500       # por encima, el aire es de mala calidad (LED rojo)
MIN_PPM, MAX_PPM = 400, 5000   # rango de set_config de la placa

VERDE, AMARILLO, ROJO = 0, 1, 2


def umbrales(config):
    """(atencion, alto) que reportó la placa; si faltan o no son válidos, los de arriba."""
    a, b = config.get("atencion_ppm"), config.get("alto_ppm")
    validos = all(isinstance(x, int) and not isinstance(x, bool) and MIN_PPM <= x <= MAX_PPM for x in (a, b))
    if validos and a < b:
        return a, b
    return ATENCION_PPM, ALTO_PPM


def nivel(co2, atencion, alto):
    if co2 > alto:
        return ROJO
    if co2 > atencion:
        return AMARILLO
    return VERDE


class ConectorCO2(Conector):
    def al_recibir_datos(self, msg, ctx):
        atencion, alto = umbrales(ctx.config_vigente())
        anterior = ctx.serie("co2_ppm")[-1:]   # la última guardada, antes de guardar esta

        guardado = ctx.guardar_medicion(msg.values, msg.ts)   # quita lo que esté fuera de rango
        if "co2_ppm" not in guardado:
            return   # no hubo medición válida: nada que comparar
        co2 = guardado["co2_ppm"]

        antes = nivel(anterior[0][1], atencion, alto) if anterior else VERDE
        ahora = nivel(co2, atencion, alto)
        if ahora == antes:
            return

        if ahora == ROJO:
            ctx.alertar(TIPO_ALERTA, "high",
                        f"CO2 alto: {co2} ppm (umbral {alto} ppm). Aire de mala calidad: ventilar ya")
        elif ahora == AMARILLO:
            if antes == ROJO:
                texto = f"CO2 bajó a {co2} ppm: sigue en atención (desde {atencion} ppm). Conviene ventilar"
            else:
                texto = f"CO2 en {co2} ppm (atención desde {atencion} ppm). Conviene ventilar"
            ctx.alertar(TIPO_ALERTA, "warning", texto)
        else:
            ctx.alertar(TIPO_ALERTA, "info", f"CO2 volvió a {co2} ppm: aire normal")
