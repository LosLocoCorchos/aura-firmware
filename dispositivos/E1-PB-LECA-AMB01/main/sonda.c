#include "sonda.h"

bool sonda_registrar(sonda_t *s, bool ok, double valor, uint64_t ahora_ms)
{
    bool cambio = false;
    if (ok) {
        s->tiene_valor = true;
        s->valor = valor;
        s->t_ms = ahora_ms;
        s->fallas = 0;
        if (s->oks < 255) {
            s->oks++;
        }
        if (s->en_falla && s->oks >= SONDA_OKS_PARA_VOLVER) {
            s->en_falla = false;
            cambio = true;
        }
    } else {
        s->oks = 0;
        if (s->fallas < 255) {
            s->fallas++;
        }
        if (!s->en_falla && s->fallas >= SONDA_FALLAS_PARA_CAER) {
            s->en_falla = true;
            cambio = true;
        }
    }
    return cambio;
}

bool sonda_vigente(const sonda_t *s, uint64_t ahora_ms, double *valor)
{
    if (s->en_falla || !s->tiene_valor || ahora_ms < s->t_ms || ahora_ms - s->t_ms > SONDA_VIGENCIA_MS) {
        return false;
    }
    *valor = s->valor;
    return true;
}

bool sonda_toca_reiniciar(uint32_t ciclos_sin_lectura)
{
    return ciclos_sin_lectura >= SONDA_REINICIO_DESDE &&
           (ciclos_sin_lectura - SONDA_REINICIO_DESDE) % SONDA_REINICIO_CADA == 0;
}
