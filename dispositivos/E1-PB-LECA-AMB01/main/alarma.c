#include "alarma.h"

static void evaluar(causa_t *c, uint64_t ahora_ms, bool ok, uint32_t valor, bool saturado, uint32_t umbral, uint32_t normal)
{
    if (ok) {
        /* Con un hueco largo entre lecturas válidas no se puede decir que "se sostuvo": la racha empieza de nuevo */
        if (c->racha && ahora_ms - c->ultima_valida_ms > ALARMA_HUECO_MAX_MS) {
            c->racha = false;
        }
        c->ultima_valida_ms = ahora_ms;
        c->valor = valor;
        c->saturado = saturado;

        bool a_activa = (c->estado == CAUSA_NORMAL) && valor >= umbral;
        bool a_normal = (c->estado != CAUSA_NORMAL) && valor < normal;
        if (a_activa || a_normal) {
            if (!c->racha || c->racha_a_normal != a_normal) {
                c->racha = true;
                c->racha_a_normal = a_normal;
                c->racha_desde_ms = ahora_ms;
            }
            if (ahora_ms - c->racha_desde_ms >= ALARMA_FILTRO_MS) {
                c->estado = a_normal ? CAUSA_NORMAL : CAUSA_ACTIVA;
                c->racha = false;
            }
        } else {
            c->racha = false;          /* en la banda de histéresis o del mismo lado: no hay racha */
        }
    }
    /* Sin lectura válida no se toca nada, salvo el reloj del silencio, que corre igual */
    if (c->estado == CAUSA_SILENCIADA && ahora_ms - c->silenciada_desde_ms >= ALARMA_SILENCIO_MAX_MS) {
        c->estado = CAUSA_ACTIVA;      /* venció: si sigue alta, vuelve a sonar */
    }
}

void alarma_actualizar(alarma_t *a, uint64_t ahora_ms, bool humo_ok, uint32_t humo_mv, bool humo_saturado,
                       bool co2_ok, uint16_t co2_ppm)
{
    evaluar(&a->humo, ahora_ms, humo_ok, humo_mv, humo_saturado, HUMO_ALARMA_MV, HUMO_NORMAL_MV);
    evaluar(&a->co2, ahora_ms, co2_ok, co2_ppm, false, CO2_ALARMA_PPM, CO2_NORMAL_PPM);
    a->activa = a->humo.estado != CAUSA_NORMAL || a->co2.estado != CAUSA_NORMAL;
}

static bool silenciar_causa(causa_t *c, uint64_t ahora_ms)
{
    if (c->estado != CAUSA_ACTIVA) {
        return false;
    }
    c->estado = CAUSA_SILENCIADA;
    c->silenciada_desde_ms = ahora_ms;
    return true;
}

bool alarma_silenciar(alarma_t *a, uint64_t ahora_ms)
{
    bool humo = silenciar_causa(&a->humo, ahora_ms);
    bool co2 = silenciar_causa(&a->co2, ahora_ms);
    return humo || co2;
}

bool alarma_buzzer(const alarma_t *a, uint64_t ahora_ms)
{
    bool hay_activa = a->humo.estado == CAUSA_ACTIVA || a->co2.estado == CAUSA_ACTIVA;
    return hay_activa && (ahora_ms % ALARMA_PITIDO_CADA_MS) < ALARMA_PITIDO_MS;
}
