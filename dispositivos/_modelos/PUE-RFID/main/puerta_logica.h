#pragma once
/*
 * Logica de la puerta (C puro, sin ESP-IDF: se prueba en la compu). ADR-009.
 *
 * Esta maquina de estados NO toca hardware ni la mesh. Recibe hechos con la hora (una tarjeta,
 * el sensor de la puerta, el paso del tiempo, una orden) y devuelve ACCIONES en una cola
 * (puerta_sacar): dar un pulso, cortarlo, un sonido, un aviso continuo, un evento para el
 * registro, una alerta. main.c las ejecuta en orden. Asi la politica queda separada de los
 * pines y se puede probar entera sin placa.
 *
 * Tiempo: milisegundos monotonicos de 64 bits (esp_timer_get_time() / 1000). No da la vuelta
 * (no hay rollover de millis() a los 49,7 dias) y aun asi se comparan diferencias con signo.
 *
 * Reglas (casos de uso U1 a U11 del anexo A del ADR):
 *  - Reposo: cerradura TRABADA. Un llavero autorizado da un pulso de corriente con tope; al
 *    abrirse la puerta se corta el pulso.
 *  - Armado con doble pasada: dos pasadas de un llavero autorizado en 3 s, con la puerta
 *    CERRADA, arman (la segunda corta el pulso). Con la puerta abierta no arma.
 *  - Armada: una pasada desarma y abre; abrir la puerta sin desarmar es una INTRUSION. La
 *    alerta de intrusion es lo primero que se emite.
 *  - Puerta abierta de mas: aviso sonoro (tope de 2 min) y alerta; al cerrarse, otra alerta info.
 *  - Muchos rechazos seguidos: se frenan las lecturas un rato y se avisa a AURA.
 *  - Sin modos de puerta abierta: no hay libre paso ni emergencia (queman la cerradura).
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "cerradura_pulso.h"
#include "lista_tarjetas.h"

/* ---------- Tiempos fijos (lo que cambia por puerta va en set_config, no aca) ---------- */
#define DOBLE_PASADA_MS 3000           /* ventana entre las dos pasadas que arman */
#define MISMA_TARJETA_MS 700           /* una tarjeta apoyada sobre el lector cuenta una sola vez */
#define TOPE_AVISO_MS 120000           /* ningun aviso suena mas de 2 min */
#define RECHAZOS_PARA_BLOQUEO 5
#define VENTANA_RECHAZOS_MS 60000
#define BLOQUEO_MS 30000
#define SENSOR_SOSPECHOSO_MS (4LL * 3600 * 1000)   /* "abierta" por horas: falla del sensor */
#define LATIDO_ARMADA_S 30
#define LATIDO_DESARMADA_S 120

/* Rangos de set_config */
#define PULSO_DEFECTO_MS 5000
#define AVISO_DEFECTO_S 15
#define AVISO_MIN_S 5
#define AVISO_MAX_S 600

typedef enum {
    SON_OK = 1,                        /* llavero autorizado (moneda) */
    SON_RECHAZO,                       /* dos pitidos graves */
    SON_ERROR,                         /* no se pudo (armar con la puerta abierta, limite de pulsos) */
    SON_ARMADO,                        /* tres notas bajando */
    SON_DESARMADO                      /* dos notas subiendo */
} sonido_t;

typedef enum {
    AVISO_NADA = 0,                    /* corta el aviso en curso */
    AVISO_PITIDOS_PULSO,               /* pi pi pi que sube mientras la cerradura esta destrabada */
    AVISO_PUERTA_ABIERTA,              /* puerta abierta de mas: sube de volumen y de ritmo */
    AVISO_INTRUSION                    /* sirena */
} aviso_t;

typedef enum {
    ACC_PULSO = 1,                     /* valor = ms que se concedieron */
    ACC_CORTAR_PULSO,
    ACC_SONIDO,                        /* valor = sonido_t */
    ACC_AVISO,                         /* valor = aviso_t; desde_ms y hasta_ms (el tope) */
    ACC_EVENTO,                        /* texto = acceso, rechazo, apertura, cierre, armado, desarmado, intrusion */
    ACC_ALERTA,                        /* texto = tipo (a-z_), severidad, mensaje */
    ACC_GUARDAR_ARMADA,                /* valor = 0/1: a NVS (solo si cambio) */
    ACC_REPORTAR                       /* hubo un cambio que AURA tiene que ver ya (armada, latido) */
} accion_tipo_t;

typedef enum { SEV_INFO = 0, SEV_WARNING, SEV_HIGH, SEV_CRITICAL } severidad_t;

typedef struct {
    accion_tipo_t tipo;
    int32_t valor;
    int64_t desde_ms, hasta_ms;
    severidad_t severidad;
    bool de_la_app;                    /* el evento vino de una orden remota y no de un llavero */
    uint8_t uid_largo;                 /* 0 = sin llavero */
    uint8_t uid[UID_MAX_BYTES];
    char texto[28];
    char mensaje[56];
} accion_t;

#define COLA_ACCIONES 24

typedef struct {
    /* Configuracion (set_config) */
    int32_t pulso_ms;
    int32_t aviso_s;
    /* Estado */
    bool armada;
    bool abierta;
    int64_t abierta_desde_ms;
    cerradura_t cerradura;
    bool doble_activa;
    int64_t doble_hasta_ms;
    bool aviso_pendiente;              /* puerta abierta: falta que pase aviso_s para avisar */
    int64_t aviso_desde_ms;
    bool aviso_sonando;
    int64_t aviso_hasta_ms;
    bool aviso_alertado;               /* ya se mando la alerta de "abierta de mas" */
    bool intrusion;
    bool intrusion_sonando;
    int64_t intrusion_hasta_ms;
    /* Rechazos seguidos */
    int64_t rechazos[RECHAZOS_PARA_BLOQUEO];
    uint8_t rechazo_siguiente;
    int64_t bloqueo_hasta_ms;
    bool bloqueado;
    /* Ultima tarjeta leida, para no contar dos veces la misma apoyada */
    uint8_t ult_uid[UID_MAX_BYTES];
    uint8_t ult_largo;
    int64_t ult_ms;
    bool hay_ult;
    /* Cola de acciones */
    accion_t cola[COLA_ACCIONES];
    uint8_t cola_ini, cola_cant;
    uint32_t perdidas;
} puerta_t;

static inline void puerta_copiar_texto(char *dst, size_t tam, const char *src)
{
    size_t i = 0;
    for (; src != NULL && src[i] != '\0' && i + 1 < tam; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static inline accion_t *puerta_nueva_accion(puerta_t *p, accion_tipo_t tipo)
{
    if (p->cola_cant >= COLA_ACCIONES) {
        p->perdidas++;
        return NULL;
    }
    accion_t *a = &p->cola[(p->cola_ini + p->cola_cant) % COLA_ACCIONES];
    p->cola_cant++;
    memset(a, 0, sizeof(*a));
    a->tipo = tipo;
    return a;
}

/* Saca la proxima accion, en el orden en que se produjeron. false si no hay. */
static inline bool puerta_sacar(puerta_t *p, accion_t *out)
{
    if (p->cola_cant == 0) return false;
    *out = p->cola[p->cola_ini];
    p->cola_ini = (uint8_t)((p->cola_ini + 1) % COLA_ACCIONES);
    p->cola_cant--;
    return true;
}

/* ---------- emisores ---------- */
static inline void p_sonido(puerta_t *p, sonido_t s)
{
    accion_t *a = puerta_nueva_accion(p, ACC_SONIDO);
    if (a) a->valor = s;
}

static inline void p_aviso(puerta_t *p, aviso_t modo, int64_t desde, int64_t hasta)
{
    accion_t *a = puerta_nueva_accion(p, ACC_AVISO);
    if (a) {
        a->valor = modo;
        a->desde_ms = desde;
        a->hasta_ms = hasta;
    }
}

static inline void p_evento(puerta_t *p, const char *texto, const uint8_t *uid, uint8_t largo, bool de_la_app)
{
    accion_t *a = puerta_nueva_accion(p, ACC_EVENTO);
    if (!a) return;
    puerta_copiar_texto(a->texto, sizeof(a->texto), texto);
    a->de_la_app = de_la_app;
    if (uid != NULL && largo > 0) {
        a->uid_largo = largo;
        memcpy(a->uid, uid, largo);
    }
}

static inline void p_alerta(puerta_t *p, const char *tipo, severidad_t sev, const char *mensaje)
{
    accion_t *a = puerta_nueva_accion(p, ACC_ALERTA);
    if (!a) return;
    puerta_copiar_texto(a->texto, sizeof(a->texto), tipo);
    puerta_copiar_texto(a->mensaje, sizeof(a->mensaje), mensaje);
    a->severidad = sev;
}

static inline void p_simple(puerta_t *p, accion_tipo_t tipo, int32_t valor)
{
    accion_t *a = puerta_nueva_accion(p, tipo);
    if (a) a->valor = valor;
}

/* Olvida los rechazos anteriores (un llavero autorizado, o un bloqueo recien armado) */
static inline void p_limpiar_rechazos(puerta_t *p)
{
    for (int i = 0; i < RECHAZOS_PARA_BLOQUEO; i++) p->rechazos[i] = -VENTANA_RECHAZOS_MS * 100LL;
}

/* Termina la intrusion (se cerro la puerta o se desarmo): corta la sirena si sonaba */
static inline void p_terminar_intrusion(puerta_t *p)
{
    p->intrusion = false;
    if (p->intrusion_sonando) {
        p->intrusion_sonando = false;
        p_aviso(p, AVISO_NADA, 0, 0);
    }
}

/* ---------- inicio ---------- */
/* Llamar una vez al arrancar, con lo guardado en NVS y el estado real del sensor. */
static inline void puerta_iniciar(puerta_t *p, int64_t ahora_ms, bool armada, bool abierta,
                                  int32_t pulso_ms, int32_t aviso_s)
{
    memset(p, 0, sizeof(*p));
    cerradura_iniciar(&p->cerradura);
    p->pulso_ms = pulso_ms;
    p->aviso_s = aviso_s;
    p->armada = armada;
    p->abierta = abierta;
    p->abierta_desde_ms = ahora_ms;
    p_limpiar_rechazos(p);
    if (armada && abierta) {
        /* U11: la placa no sabe que paso mientras estuvo apagada. Alerta propia y sin sonido. */
        p_alerta(p, "arranque_puerta_abierta", SEV_HIGH, "Arranco armada y con la puerta abierta");
    } else if (abierta) {
        p->aviso_pendiente = true;
        p->aviso_desde_ms = ahora_ms + (int64_t)aviso_s * 1000;
    }
}

static inline int puerta_latido_s(const puerta_t *p)
{
    return p->armada ? LATIDO_ARMADA_S : LATIDO_DESARMADA_S;
}

/* El sensor marca "abierta" por horas: probablemente esta trabado (F9). */
static inline bool puerta_sensor_sospechoso(const puerta_t *p, int64_t ahora_ms)
{
    return p->abierta && (ahora_ms - p->abierta_desde_ms) >= SENSOR_SOSPECHOSO_MS;
}

/* ---------- pulso ---------- */
static inline void p_cortar_pulso(puerta_t *p, int64_t ahora_ms)
{
    if (!p->cerradura.energizada) return;        /* la unica verdad sobre el pulso en curso */
    cerradura_cortar(&p->cerradura, ahora_ms);
    p_simple(p, ACC_CORTAR_PULSO, 0);
    p_aviso(p, AVISO_NADA, 0, 0);
}

/* Pide un pulso. false si el limite por minuto no lo deja (se avisa con error y alerta). */
static inline bool p_dar_pulso(puerta_t *p, int64_t ahora_ms)
{
    int64_t concedido = cerradura_pedir(&p->cerradura, ahora_ms, p->pulso_ms, p->pulso_ms);
    if (concedido <= 0) {
        p_sonido(p, SON_ERROR);
        p_alerta(p, "limite_pulsos", SEV_WARNING, "Se llego al limite de pulsos por minuto de la cerradura");
        return false;
    }
    p_simple(p, ACC_PULSO, (int32_t)concedido);
    p_aviso(p, AVISO_PITIDOS_PULSO, ahora_ms, ahora_ms + concedido);
    return true;
}

static inline void p_programar_aviso_abierta(puerta_t *p, int64_t ahora_ms)
{
    p->aviso_pendiente = true;
    p->aviso_desde_ms = ahora_ms + (int64_t)p->aviso_s * 1000;
}

static inline void p_callar_aviso_abierta(puerta_t *p)
{
    if (p->aviso_sonando) {
        p->aviso_sonando = false;
        p_aviso(p, AVISO_NADA, 0, 0);
    }
    p->aviso_pendiente = false;
}

/* ---------- sensor de la puerta ---------- */
static inline void puerta_sensor(puerta_t *p, int64_t ahora_ms, bool abierta)
{
    if (abierta == p->abierta) return;
    p->abierta = abierta;

    if (abierta) {
        p->abierta_desde_ms = ahora_ms;
        if (p->armada) {
            /* U10: la alerta es LO PRIMERO que se emite, antes del sonido y del registro */
            p->intrusion = true;
            p->intrusion_sonando = true;
            p->intrusion_hasta_ms = ahora_ms + TOPE_AVISO_MS;
            p_alerta(p, "intrusion", SEV_CRITICAL, "Puerta abierta con el sistema armado");
            p_aviso(p, AVISO_INTRUSION, ahora_ms, p->intrusion_hasta_ms);
            p_evento(p, "intrusion", NULL, 0, false);
            return;
        }
        /* Desarmada: la salida por el picaporte, o la entrada con llavero (se corta el pulso) */
        p_cortar_pulso(p, ahora_ms);
        p_evento(p, "apertura", NULL, 0, false);
        p_programar_aviso_abierta(p, ahora_ms);
        return;
    }

    /* Se cerro */
    p_evento(p, "cierre", NULL, 0, false);
    if (p->intrusion) {
        p_terminar_intrusion(p);
        p_alerta(p, "puerta_cerrada", SEV_INFO, "Se cerro la puerta despues de la intrusion");
    }
    if (p->aviso_alertado) {
        p->aviso_alertado = false;
        p_alerta(p, "puerta_cerrada", SEV_INFO, "La puerta que estaba abierta de mas se cerro");
    }
    p_callar_aviso_abierta(p);
}

/* Cambio el TIPO de sensor (set_config): la lectura nueva es como se ve la puerta ahora, no un
   movimiento de la puerta. Se toma como estado sin eventos ni alertas (una armada que "pasa a
   abierta" por reinterpretar el pin no es una intrusion). */
static inline void puerta_sensor_resincronizar(puerta_t *p, int64_t ahora_ms, bool abierta)
{
    if (abierta == p->abierta) return;
    p->abierta = abierta;
    p->abierta_desde_ms = ahora_ms;
    if (abierta && !p->armada) {
        p_programar_aviso_abierta(p, ahora_ms);
    } else if (!abierta) {
        p_callar_aviso_abierta(p);
        p->aviso_alertado = false;
    }
}

/* ---------- tarjeta ---------- */
static inline void p_registrar_rechazo(puerta_t *p, int64_t ahora_ms)
{
    p->rechazos[p->rechazo_siguiente] = ahora_ms;
    p->rechazo_siguiente = (uint8_t)((p->rechazo_siguiente + 1) % RECHAZOS_PARA_BLOQUEO);
    /* el que sigue es el mas viejo de los ultimos RECHAZOS_PARA_BLOQUEO */
    int64_t el_mas_viejo = p->rechazos[p->rechazo_siguiente];
    if ((ahora_ms - el_mas_viejo) < VENTANA_RECHAZOS_MS) {
        p->bloqueado = true;
        p->bloqueo_hasta_ms = ahora_ms + BLOQUEO_MS;
        p_alerta(p, "intentos_rechazados", SEV_HIGH, "Muchos llaveros rechazados seguidos: lector frenado");
        p_limpiar_rechazos(p);
    }
}

/* Una pasada autorizada abre: da el pulso si la puerta esta cerrada. El acceso (evento y
   sonido de OK) se registra SOLO si de verdad abrio: si el limite de pulsos por minuto no deja
   dar el pulso, p_dar_pulso ya emitio el error y la alerta, y no hubo ningun acceso. Devuelve
   si hubo acceso (quien llama abre la ventana de la doble pasada solo en ese caso). */
static inline bool p_acceso(puerta_t *p, int64_t ahora_ms, const uint8_t *uid, uint8_t largo, bool de_la_app)
{
    if (p->abierta) {
        p_programar_aviso_abierta(p, ahora_ms);      /* pasaron la tarjeta: el plazo del aviso arranca de nuevo */
        if (p->aviso_sonando) {
            p->aviso_sonando = false;
            p_aviso(p, AVISO_NADA, 0, 0);
        }
    } else if (!p_dar_pulso(p, ahora_ms)) {
        return false;
    }
    p_evento(p, "acceso", uid, largo, de_la_app);
    p_sonido(p, SON_OK);
    return true;
}

static inline void p_desarmar(puerta_t *p, int64_t ahora_ms, const uint8_t *uid, uint8_t largo, bool de_la_app)
{
    p->armada = false;
    p_simple(p, ACC_GUARDAR_ARMADA, 0);
    p_evento(p, "desarmado", uid, largo, de_la_app);
    p_simple(p, ACC_REPORTAR, 0);                    /* el latido deja de ser el de armada */
    if (p->intrusion) {                              /* corta la sirena */
        p_terminar_intrusion(p);
        if (p->abierta) p_programar_aviso_abierta(p, ahora_ms);
    }
}

static inline void puerta_tarjeta(puerta_t *p, int64_t ahora_ms, const uint8_t *uid, uint8_t largo, bool autorizada)
{
    if (!uid_largo_valido(largo)) return;
    /* U3: la misma tarjeta apoyada cuenta una sola vez */
    if (p->hay_ult && p->ult_largo == largo && memcmp(p->ult_uid, uid, largo) == 0 &&
        (ahora_ms - p->ult_ms) < MISMA_TARJETA_MS) {
        return;
    }
    p->hay_ult = true;
    p->ult_largo = largo;
    memcpy(p->ult_uid, uid, largo);
    p->ult_ms = ahora_ms;

    /* U4: lector frenado */
    if (p->bloqueado) {
        if ((ahora_ms - p->bloqueo_hasta_ms) < 0) return;
        p->bloqueado = false;
    }

    if (!autorizada) {
        p_evento(p, "rechazo", uid, largo, false);   /* U2: queda el UID, para dar de alta */
        p_sonido(p, SON_RECHAZO);
        p_registrar_rechazo(p, ahora_ms);
        return;
    }
    p_limpiar_rechazos(p);

    if (p->armada) {
        /* U9: una pasada desarma (y corta la sirena si sonaba). Si la puerta esta cerrada, tambien abre. */
        p_desarmar(p, ahora_ms, uid, largo, false);
        if (p_acceso(p, ahora_ms, uid, largo, false)) {
            p->doble_activa = true;
            p->doble_hasta_ms = ahora_ms + DOBLE_PASADA_MS;
        }
        return;
    }

    if (p->doble_activa && (ahora_ms - p->doble_hasta_ms) < 0) {
        /* Segunda pasada seguida: quiere armar */
        p->doble_activa = false;
        if (p->abierta) {                            /* U8 */
            p_sonido(p, SON_ERROR);
            return;
        }
        p_cortar_pulso(p, ahora_ms);                 /* la segunda corta el pulso */
        p->armada = true;
        p_simple(p, ACC_GUARDAR_ARMADA, 1);
        p_evento(p, "armado", uid, largo, false);
        p_sonido(p, SON_ARMADO);
        p_simple(p, ACC_REPORTAR, 0);
        return;
    }

    if (p_acceso(p, ahora_ms, uid, largo, false)) {
        p->doble_activa = true;
        p->doble_hasta_ms = ahora_ms + DOBLE_PASADA_MS;
    }
}

/* Orden remota de abrir (solo si se compilo habilitada). Desarma y abre; no cuenta para la
   doble pasada. La vigencia de la orden (vencimiento, hora valida) la controla quien la recibe. */
static inline void puerta_abrir_remoto(puerta_t *p, int64_t ahora_ms)
{
    if (p->armada) p_desarmar(p, ahora_ms, NULL, 0, true);
    p_acceso(p, ahora_ms, NULL, 0, true);
    p->doble_activa = false;
}

/* ---------- el paso del tiempo ---------- */
static inline void puerta_tick(puerta_t *p, int64_t ahora_ms)
{
    if (cerradura_vencio(&p->cerradura, ahora_ms)) {
        p_cortar_pulso(p, ahora_ms);
    }
    if (p->aviso_pendiente && p->abierta && !p->armada && (ahora_ms - p->aviso_desde_ms) >= 0) {
        p->aviso_pendiente = false;
        p->aviso_sonando = true;
        p->aviso_alertado = true;
        p->aviso_hasta_ms = ahora_ms + TOPE_AVISO_MS;
        p_aviso(p, AVISO_PUERTA_ABIERTA, ahora_ms, p->aviso_hasta_ms);
        p_alerta(p, "puerta_abierta", SEV_WARNING, "La puerta quedo abierta demasiado tiempo");
    }
    if (p->aviso_sonando && (ahora_ms - p->aviso_hasta_ms) >= 0) {
        p->aviso_sonando = false;                    /* tope de 2 min: sigue la alerta, calla el sonido */
        p_aviso(p, AVISO_NADA, 0, 0);
    }
    if (p->intrusion_sonando && (ahora_ms - p->intrusion_hasta_ms) >= 0) {
        p->intrusion_sonando = false;
        p_aviso(p, AVISO_NADA, 0, 0);
    }
}

/* ---------- Vigencia de una orden remota (A6) ----------
 * `abrir` llega con un vencimiento (epoch en segundos). La placa la rechaza si no tiene hora
 * valida (no puede saber si esta vencida), si ya vencio, o si el vencimiento es absurdamente
 * lejano (una orden encolada en el broker no se tiene que poder ejecutar horas despues). */
#define HORA_VALIDA_DESDE_S 1735689600LL    /* 2025-01-01 */
#define ORDEN_VIGENCIA_MAX_S 300

typedef enum {
    ORDEN_OK = 0,
    ORDEN_SIN_VENCIMIENTO,
    ORDEN_SIN_HORA,
    ORDEN_VENCIDA,
    ORDEN_VENCIMIENTO_LEJANO
} orden_estado_t;

static inline orden_estado_t orden_vigente(bool tiene_vence, int64_t vence_s, int64_t ahora_s)
{
    if (!tiene_vence) return ORDEN_SIN_VENCIMIENTO;
    if (ahora_s < HORA_VALIDA_DESDE_S) return ORDEN_SIN_HORA;
    if (vence_s <= ahora_s) return ORDEN_VENCIDA;
    if (vence_s - ahora_s > ORDEN_VIGENCIA_MAX_S) return ORDEN_VENCIMIENTO_LEJANO;
    return ORDEN_OK;
}
