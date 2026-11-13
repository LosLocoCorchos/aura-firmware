"""Conector AURA v4: mediciones de heladera/freezer y alertas por cruce.

No usa red, archivos ni estado de instancia. Las decisiones se apoyan en
config_vigente() y serie(): sobreviven a una recreación del conector.
El estado se reconstruye con los umbrales vigentes. Si cambiaron, puede diferir
del estado que reflejan las alertas emitidas con la configuración anterior.
"""
from datetime import datetime, timedelta, timezone
from math import isfinite

from aura_sdk import Conector

CAMPOS = {
    "temp_heladera_c": ("heladera", "hlo", "hhi", 2.0, 6.0),
    "temp_freezer_c": ("freezer", "flo", "fhi", -25.0, -15.0),
}

VENTANA_RECIENTE = timedelta(days=1)
HORIZONTES_ANTERIORES = (timedelta(days=3), timedelta(days=7))
UN_MICROSEGUNDO = timedelta(microseconds=1)


def numero(valor):
    return (isinstance(valor, int) and not isinstance(valor, bool)) or (isinstance(valor, float) and isfinite(valor))


def zona_umbral(valor, inferior, superior):
    return -1 if valor < inferior else 1 if valor > superior else 0


def recupera(lado, valor, inferior, superior, margen):
    if lado < 0:
        return inferior + margen <= valor <= superior
    if lado > 0:
        return inferior <= valor <= superior - margen
    return False


def estado_anterior(ctx, campo, instante, llegada, inferior, superior, margen):
    referencia = instante or llegada
    desde = referencia - VENTANA_RECIENTE
    # Para lecturas fechadas, mirar también si ya se guardó una muestra posterior.
    hasta = referencia + VENTANA_RECIENTE if instante is not None else llegada
    puntos = ctx.serie(campo, desde=desde, hasta=hasta)
    if instante is not None:
        if puntos and hora_utc(puntos[-1][0]) > instante:
            return None
        if hasta < llegada and ctx.serie(campo, desde=max(hasta, llegada - VENTANA_RECIENTE), hasta=llegada):
            return None

    ultima_zona = None
    zona_siguiente = 0
    recuperadas = set()
    horizontes = iter(HORIZONTES_ANTERIORES)
    while True:
        for _, temperatura in reversed(puntos):
            # El runtime puede guardar la muestra cruda si el conector falla.
            if not numero(temperatura) or not -55 <= temperatura <= 125 or temperatura == 85:
                continue
            medida = round(temperatura * 100)
            zona = zona_umbral(medida, inferior, superior)
            if ultima_zona is None:
                ultima_zona = zona
            if zona != 0 and zona == zona_siguiente:
                # El par más reciente confirma el lado, salvo recuperación posterior.
                return (0 if zona in recuperadas else zona), ultima_zona
            if zona == 0:
                for lado in (-1, 1):
                    if recupera(lado, medida, inferior, superior, margen):
                        recuperadas.add(lado)
                if len(recuperadas) == 2:
                    return 0, ultima_zona
            zona_siguiente = zona

        horizonte = next(horizontes, None)
        if horizonte is None:
            return 0, ultima_zona or 0
        hasta = desde - UN_MICROSEGUNDO  # Ambos límites de serie() son inclusivos.
        desde = referencia - horizonte
        puntos = ctx.serie(campo, desde=desde, hasta=hasta)


def hora_utc(ts):
    if ts is None:
        return None
    return ts.replace(tzinfo=timezone.utc) if ts.tzinfo is None else ts.astimezone(timezone.utc)


class ConectorHeladeraFreezer(Conector):
    def al_recibir_datos(self, msg, ctx):
        validos = {}
        for campo in CAMPOS:
            valor = msg.values.get(campo)
            limites = self.manifiesto.campos[campo]
            if numero(valor) and limites.min <= valor <= limites.max and valor != 85:
                validos[campo] = valor
            elif campo in msg.values:
                ctx.log.warning("%s: valor fisicamente invalido; se omite", campo)

        if not validos:
            ctx.descartar("sin_temperaturas_validas")
            return

        config = ctx.config_vigente()
        instante = hora_utc(msg.ts)
        llegada = datetime.now(timezone.utc)

        alertas = {}
        # Consultar lo ya guardado antes de agregar esta medición al historial.
        for campo, valor in validos.items():
            nombre, kmin, kmax, default_min, default_max = CAMPOS[campo]
            inferior, superior = config.get(kmin, default_min), config.get(kmax, default_max)
            if not (numero(inferior) and numero(superior) and -55 <= inferior < superior <= 125):
                ctx.log.warning("%s: config de umbrales invalida; se usan valores iniciales", campo)
                inferior, superior = default_min, default_max

            # Trabajar en centésimas evita diferencias en los límites con el firmware.
            minimo, maximo = round(inferior * 100), round(superior * 100)
            margen = min(50, (maximo - minimo) / 2)
            estado = estado_anterior(ctx, campo, instante, llegada, minimo, maximo, margen)
            if estado is None:
                ctx.log.info("%s: lectura histórica guardada sin emitir alertas", campo)
                continue
            activa, pendiente = estado
            medida = round(valor * 100)
            zona = zona_umbral(medida, minimo, maximo)
            actual = zona if zona != 0 and zona == pendiente else activa
            if recupera(activa, medida, minimo, maximo, margen):
                actual = 0
            if actual == activa:
                continue

            fecha = f" (medición {instante.isoformat()})" if instante is not None else " (sin hora de medición)"
            if actual == 0:
                nivel = "info"
                texto = f"{nombre.capitalize()} recuperada: {valor} °C"
            else:
                nivel = "warning"
                texto = f"{nombre.capitalize()}: {valor} °C, fuera de {inferior}..{superior} °C"
            alertas[campo] = (f"temperatura_{nombre}", nivel, texto + fecha)

        for campo in ctx.guardar_medicion(validos, msg.ts):
            if campo in alertas:
                ctx.alertar(*alertas[campo])
