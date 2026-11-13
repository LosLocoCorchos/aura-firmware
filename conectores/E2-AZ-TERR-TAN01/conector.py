"""Conector de E2-AZ-TERR-TAN01 — nivel de 3 tanques de agua (terraza del Edificio 2).

El nodo mide los tres tanques juntos cada `intervalo_s` y los manda en un mensaje. Por tanque
llegan `distancia_tanqueN_cm` y `nivel_tanqueN_pct`, que el nodo calcula con la altura y el lleno
del tanque (de fábrica, los del Mayper de 5000 L; se cambian por `set_config` desde AURA).

Alertas:
- `tanque_bajo`: el nivel bajó del mínimo (20 %) y, con un margen, cuando se recupera. Usa el
  nivel: un mensaje sin nivel no cuenta. El nodo solo mide; la bomba la maneja
  un control externo, y este aviso sirve para saber que no llenó a tiempo.
- `sensor`: un tanque dejó de llegar mientras los otros siguen llegando. El nodo no manda un
  tanque cuyo sensor no midió, así que la falla se nota porque falta en los mensajes. Si fallan
  los tres, el nodo manda un mensaje de estado que el codec traduce a `errors` (no llega acá).

`25` cm de distancia llega cuando el agua está en la zona ciega del sensor (tanque lleno): no es
una falla. Lo que está fuera de los rangos de conector.toml lo quita AURA al guardar.

No guarda estado en la instancia: AURA la puede recrear. Lo que necesita saber de antes lo lee
de las mediciones guardadas, con ctx.serie().
"""
from datetime import datetime, timedelta, timezone

from aura_sdk import Conector

N_TANQUES = 3
DISTANCIAS = tuple(f"distancia_tanque{n}_cm" for n in range(1, N_TANQUES + 1))
NIVELES = tuple(f"nivel_tanque{n}_pct" for n in range(1, N_TANQUES + 1))


class ConectorTanques(Conector):
    # Nivel (%) por debajo del cual se avisa `tanque_bajo`.
    NIVEL_MINIMO_PCT = 20
    # El aviso de que se recuperó sale recién cuando el nivel sube este margen sobre el mínimo:
    # así no se repiten avisos si el nivel queda justo en el borde.
    HISTERESIS_PCT = 5
    # Mensajes con otros tanques y sin noticias de uno antes de dar su sensor por caído. Llega
    # un mensaje por ciclo: 3 son 3 ciclos seguidos, sea cual sea el intervalo, y no avisa por
    # una medición perdida.
    MENSAJES_SIN_DATOS = 3
    # Hasta dónde mira hacia atrás: primero poco (lo normal) y, si no alcanza, más.
    VENTANAS = (timedelta(hours=2), timedelta(days=3))
    # Para decidir si un tanque estaba bajo, si toda la historia cae en la banda de histéresis.
    VENTANAS_NIVEL = (timedelta(days=3), timedelta(days=30))

    def al_recibir_datos(self, msg, ctx):
        ts = msg.ts or datetime.now(timezone.utc)
        # Lo guardado ANTES de este mensaje.
        historia = self._historia(ctx, ts)

        guardado = ctx.guardar_medicion(msg.values, msg.ts)   # quita lo inválido
        if not guardado:
            return   # no quedó nada válido: AURA descarta el mensaje (sin_campos_validos)

        self._avisar_sensores(guardado, historia, ctx)
        self._avisar_nivel(guardado, historia, ctx, ts)

    # ---------------------------------------------------------------- historia

    def _historia(self, ctx, ts):
        """Las series de distancia y nivel antes de este mensaje, con la ventana que alcance.

        Alcanza la ventana corta si en ella cada tanque tiene una distancia y suficientes
        mensajes de los otros para decidir; si no (tanque caído hace mucho, intervalo largo),
        se usa la larga.
        """
        for ventana in self.VENTANAS:
            desde = ts - ventana
            h = {c: ctx.serie(c, desde=desde) for c in DISTANCIAS + NIVELES}
            if all(h[c] for c in DISTANCIAS) and all(
                    len(self._mensajes_de_otros(h, i)) >= self.MENSAJES_SIN_DATOS for i in range(N_TANQUES)):
                return h
        return h

    @staticmethod
    def _mensajes_de_otros(h, indice, despues=None):
        """Instantes de los mensajes que trajeron otro tanque, posteriores a `despues`."""
        instantes = set()
        for j, campo in enumerate(DISTANCIAS):
            if j == indice:
                continue
            instantes.update(t for t, _ in h[campo] if despues is None or t > despues)
        return instantes

    # ---------------------------------------------------------------- sensor

    def _caido(self, h, indice, extra=()):
        """Si el tanque está caído: los otros mandaron MENSAJES_SIN_DATOS mensajes desde su
        última distancia (o, si nunca llegó una en la ventana, desde el principio de ella)."""
        ultimo = h[DISTANCIAS[indice]][-1][0] if h[DISTANCIAS[indice]] else None
        otros = self._mensajes_de_otros(h, indice, ultimo) | set(extra)
        return len(otros) >= self.MENSAJES_SIN_DATOS

    def _avisar_sensores(self, guardado, h, ctx):
        """Alerta `sensor` una vez al caer y una vez al volver, sin guardar estado."""
        ahora = object()   # este mensaje cuenta como uno más de los otros tanques
        for indice, campo in enumerate(DISTANCIAS):
            numero = indice + 1
            caido_antes = self._caido(h, indice)
            if campo in guardado:
                if caido_antes:
                    ctx.alertar("sensor", "info", f"Tanque {numero}: el sensor volvió a medir")
            elif not caido_antes and self._caido(h, indice, extra=(ahora,)):
                ctx.alertar("sensor", "warning",
                            f"Tanque {numero}: el sensor no da mediciones válidas "
                            f"({self.MENSAJES_SIN_DATOS} mensajes de los otros tanques sin noticias de este)")

    # ---------------------------------------------------------------- nivel

    def _decidir(self, puntos):
        """El último nivel fuera de la banda de histéresis dice si estaba bajo: por debajo del
        mínimo, sí; en el mínimo + histéresis o más, no. None si todos caen en la banda."""
        for _, valor in reversed(puntos):
            if valor < self.NIVEL_MINIMO_PCT:
                return True
            if valor >= self.NIVEL_MINIMO_PCT + self.HISTERESIS_PCT:
                return False
        return None

    def _estaba_bajo(self, ctx, campo, puntos, ts):
        """Si el tanque estaba bajo, según los niveles anteriores y sin guardar estado.

        Si todo lo de la ventana corta cae en la banda de histéresis (el nivel quedó horas entre
        el mínimo y el mínimo + histéresis), busca más atrás. Si nunca salió de la banda, o no
        hay historia, no estaba bajo.
        """
        decision = self._decidir(puntos)
        for ventana in self.VENTANAS_NIVEL:
            if decision is not None:
                break
            anteriores = [(t, v) for t, v in ctx.serie(campo, desde=ts - ventana) if t < ts]
            decision = self._decidir(anteriores)
        return bool(decision)

    def _avisar_nivel(self, guardado, h, ctx, ts):
        for indice, campo in enumerate(NIVELES):
            if campo not in guardado:
                continue   # no vino en este mensaje
            numero = indice + 1
            nivel = guardado[campo]
            estaba_bajo = self._estaba_bajo(ctx, campo, h[campo], ts)
            if estaba_bajo:
                esta_bajo = nivel < self.NIVEL_MINIMO_PCT + self.HISTERESIS_PCT
            else:
                esta_bajo = nivel < self.NIVEL_MINIMO_PCT
            if esta_bajo and not estaba_bajo:
                ctx.alertar("tanque_bajo", "warning",
                            f"Tanque {numero}: queda {nivel} % de agua (mínimo {self.NIVEL_MINIMO_PCT} %)")
            elif not esta_bajo and estaba_bajo:
                ctx.alertar("tanque_bajo", "info", f"Tanque {numero}: el nivel volvió a {nivel} %")
