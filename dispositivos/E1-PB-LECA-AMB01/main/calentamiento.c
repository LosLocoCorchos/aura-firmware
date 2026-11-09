#include "calentamiento.h"

static uint64_t transcurrido_ms(uint64_t desde_ms, uint64_t ahora_ms)
{
    return ahora_ms > desde_ms ? ahora_ms - desde_ms : 0;
}

bool calentamiento_activo(uint64_t desde_ms, uint64_t ahora_ms, uint32_t duracion_s)
{
    return transcurrido_ms(desde_ms, ahora_ms) < (uint64_t)duracion_s * 1000u;
}

uint32_t calentamiento_restante_s(uint64_t desde_ms, uint64_t ahora_ms, uint32_t duracion_s)
{
    uint64_t total_ms = (uint64_t)duracion_s * 1000u;
    uint64_t t = transcurrido_ms(desde_ms, ahora_ms);
    if (t >= total_ms) {
        return 0;
    }
    return (uint32_t)((total_ms - t + 999u) / 1000u);
}
