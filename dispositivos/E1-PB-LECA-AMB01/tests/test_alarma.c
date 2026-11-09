#include <string.h>
#include "alarma.h"
#include "aserciones.h"

#define PASO_MS 5000u                      /* el firmware lee cada 5 s */
#define MIN_MS  60000u

/* Reloj y estado de la simulación */
static alarma_t a;
static uint64_t t;

static void reiniciar(void)
{
    memset(&a, 0, sizeof a);
    t = 100000;                            /* no arranca en 0: da lo mismo */
}

/* Una lectura de cada sonda, y pasan 5 s */
static void leer(bool humo_ok, uint32_t humo, bool co2_ok, uint16_t co2)
{
    alarma_actualizar(&a, t, humo_ok, humo, false, co2_ok, co2);
    t += PASO_MS;
}

/* Lecturas válidas iguales durante ms (incluye la primera y la última) */
static void sostener(uint32_t humo, uint16_t co2, uint64_t ms)
{
    for (uint64_t fin = t + ms; t <= fin;) {
        leer(true, humo, true, co2);
    }
}

int main(void)
{
    const uint32_t H_ALTO = 3000, H_BAJO = 900;
    const uint16_t C_ALTO = 1800, C_BAJO = 600;

    /* --- Filtro de 1 minuto para prender -------------------------------------------------- */
    reiniciar();
    sostener(H_BAJO, C_BAJO, 5 * MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_NORMAL && a.humo.estado == CAUSA_NORMAL && !a.activa);

    /* Un pico corto no prende el aviso: lecturas altas durante menos de 1 min */
    for (int i = 0; i < 12; i++) {                                 /* 0..55 s */
        leer(true, H_BAJO, true, C_ALTO);
        VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    }
    leer(true, H_BAJO, true, C_ALTO);                              /* 60 s sostenidos: prende */
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA && a.activa && a.humo.estado == CAUSA_NORMAL);
    VERIFICAR(a.co2.valor == C_ALTO);

    /* Los umbrales son inclusivos (>=) */
    reiniciar();
    sostener(H_BAJO, CO2_ALARMA_PPM - 1, 2 * MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    sostener(H_BAJO, CO2_ALARMA_PPM, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    reiniciar();
    sostener(HUMO_ALARMA_MV - 1, C_BAJO, 2 * MIN_MS);
    VERIFICAR(a.humo.estado == CAUSA_NORMAL);
    sostener(HUMO_ALARMA_MV, C_BAJO, MIN_MS);
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA);

    /* Una lectura que baja del umbral corta la racha: el minuto empieza de nuevo */
    reiniciar();
    sostener(H_BAJO, C_ALTO, 30000);                               /* 30 s altos */
    leer(true, H_BAJO, true, CO2_ALARMA_PPM - 1);                  /* una por debajo del umbral */
    sostener(H_BAJO, C_ALTO, 50000);                               /* otros 50 s: en total más de 1 min, pero no seguidos */
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    sostener(H_BAJO, C_ALTO, 15000);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);

    /* --- Filtro de 1 minuto para apagar, con histéresis ------------------------------------ */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    sostener(H_BAJO, CO2_ALARMA_PPM - 1, 5 * MIN_MS);              /* entre normal y umbral: sigue activa */
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    sostener(H_BAJO, CO2_NORMAL_PPM, 5 * MIN_MS);                  /* justo en el valor normal: todavía no es "bajo" */
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    for (int i = 0; i < 12; i++) {                                 /* 55 s bajo el valor normal */
        leer(true, H_BAJO, true, CO2_NORMAL_PPM - 1);
        VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    }
    leer(true, H_BAJO, true, CO2_NORMAL_PPM - 1);                  /* 1 min: se normaliza */
    VERIFICAR(a.co2.estado == CAUSA_NORMAL && !a.activa);
    /* Un rebote alto en medio de la bajada reinicia el minuto */
    reiniciar();
    sostener(HUMO_ALARMA_MV, C_BAJO, MIN_MS);
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA);
    sostener(HUMO_NORMAL_MV - 1, C_BAJO, 30000);
    leer(true, HUMO_ALARMA_MV, true, C_BAJO);
    sostener(HUMO_NORMAL_MV - 1, C_BAJO, 50000);
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA);
    sostener(HUMO_NORMAL_MV - 1, C_BAJO, 15000);
    VERIFICAR(a.humo.estado == CAUSA_NORMAL);

    /* --- Sin lectura válida la causa queda como estaba ------------------------------------- */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    for (int i = 0; i < 100; i++) {
        leer(true, H_BAJO, false, 0);                              /* el SCD40 no responde 8 min */
    }
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA && a.activa);           /* no se apagó */
    reiniciar();
    for (int i = 0; i < 100; i++) {
        leer(true, H_BAJO, false, 0);
    }
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);                       /* y la que estaba normal tampoco se prende sola */
    /* Un hueco largo descarta la racha: no se suma lo de antes con lo de después */
    reiniciar();
    sostener(H_BAJO, C_ALTO, 30000);
    for (int i = 0; i < 20; i++) {
        leer(true, H_BAJO, false, 0);                              /* 100 s sin dato */
    }
    leer(true, H_BAJO, true, C_ALTO);
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    /* Una sonda sin dato no tapa a la otra */
    reiniciar();
    for (uint64_t fin = t + MIN_MS; t <= fin;) {
        leer(true, H_ALTO, false, 0);
    }
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA && a.co2.estado == CAUSA_NORMAL);

    /* --- Silencio por causa: callar el CO2 no deja mudo el aviso ante una causa nueva (humo) --- */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    VERIFICAR(alarma_silenciar(&a, t));
    VERIFICAR(a.co2.estado == CAUSA_SILENCIADA && a.activa);       /* el LED sigue en alarma */
    VERIFICAR(!alarma_buzzer(&a, 0));
    sostener(H_ALTO, C_ALTO, MIN_MS);                              /* aparece humo con el CO2 todavía alto */
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA && a.co2.estado == CAUSA_SILENCIADA);
    VERIFICAR(alarma_buzzer(&a, 0));                               /* la causa nueva sí suena */
    VERIFICAR(alarma_silenciar(&a, t));                            /* callar de nuevo calla solo la activa */
    VERIFICAR(a.humo.estado == CAUSA_SILENCIADA && a.co2.estado == CAUSA_SILENCIADA);
    VERIFICAR(!alarma_buzzer(&a, 0));
    sostener(H_ALTO, C_BAJO, MIN_MS);                              /* el CO2 se normaliza, el humo sigue callado */
    VERIFICAR(a.co2.estado == CAUSA_NORMAL && a.humo.estado == CAUSA_SILENCIADA);
    sostener(H_ALTO, C_ALTO, MIN_MS);                              /* el CO2 vuelve a subir: causa nueva, suena */
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA && alarma_buzzer(&a, 0));

    /* --- Un silence sin causa activa se rechaza y no se guarda ------------------------------ */
    reiniciar();
    VERIFICAR(!alarma_silenciar(&a, t));                           /* p. ej. durante el calentamiento */
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);                       /* la primera alarma suena igual */
    VERIFICAR(alarma_buzzer(&a, 0));
    reiniciar();
    sostener(H_BAJO, C_ALTO, 55000);                               /* 5 s antes de que prenda */
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    VERIFICAR(!alarma_silenciar(&a, t));
    leer(true, H_BAJO, true, C_ALTO);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    VERIFICAR(alarma_silenciar(&a, t));                            /* ya activa: ahora sí se acepta */
    VERIFICAR(!alarma_silenciar(&a, t));                           /* y con todo ya callado no hay nada que callar */
    /* silence cuando la alarma ya pasó */
    sostener(H_BAJO, C_BAJO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    VERIFICAR(!alarma_silenciar(&a, t));
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);

    /* --- El silencio vence a los 30 minutos ------------------------------------------------- */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(alarma_silenciar(&a, t));
    uint64_t t_silencio = t;
    while (t < t_silencio + ALARMA_SILENCIO_MAX_MS - PASO_MS) {
        leer(true, H_BAJO, true, C_ALTO);
        VERIFICAR(a.co2.estado == CAUSA_SILENCIADA);
    }
    while (t < t_silencio + ALARMA_SILENCIO_MAX_MS) {
        leer(true, H_BAJO, true, C_ALTO);
    }
    leer(true, H_BAJO, true, C_ALTO);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA && alarma_buzzer(&a, 0)); /* sigue alta: vuelve a sonar */
    /* El silencio vence aunque la sonda no dé dato */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(alarma_silenciar(&a, t));
    t_silencio = t;
    while (t < t_silencio + ALARMA_SILENCIO_MAX_MS + PASO_MS) {
        leer(true, H_BAJO, false, 0);
    }
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA);
    /* Si se normaliza antes de vencer, el silencio se va con la causa */
    reiniciar();
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(alarma_silenciar(&a, t));
    sostener(H_BAJO, C_BAJO, 2 * MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_NORMAL);
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(a.co2.estado == CAUSA_ACTIVA && alarma_buzzer(&a, 0)); /* la próxima alarma suena */

    /* --- ADC saturado: lectura válida y alta, "al menos" ------------------------------------ */
    reiniciar();
    for (uint64_t fin = t + MIN_MS; t <= fin;) {
        alarma_actualizar(&a, t, true, 4083, true, true, C_BAJO);
        t += PASO_MS;
    }
    VERIFICAR(a.humo.estado == CAUSA_ACTIVA && a.humo.saturado && a.humo.valor == 4083);
    alarma_actualizar(&a, t, true, 3000, false, true, C_BAJO);
    VERIFICAR(!a.humo.saturado);                                    /* la lectura siguiente ya no lo está */

    /* --- Buzzer: pitidos cortos cada tanto, no sirena continua ------------------------------ */
    reiniciar();
    VERIFICAR(!alarma_buzzer(&a, 0));                               /* normal: callado */
    sostener(H_BAJO, C_ALTO, MIN_MS);
    VERIFICAR(alarma_buzzer(&a, 0));                                /* al inicio del ciclo suena */
    VERIFICAR(alarma_buzzer(&a, ALARMA_PITIDO_MS - 1));
    VERIFICAR(!alarma_buzzer(&a, ALARMA_PITIDO_MS));                /* el pitido es corto */
    VERIFICAR(!alarma_buzzer(&a, ALARMA_PITIDO_CADA_MS - 1));
    VERIFICAR(alarma_buzzer(&a, ALARMA_PITIDO_CADA_MS));            /* y se repite cada tanto */
    VERIFICAR(alarma_buzzer(&a, 7 * ALARMA_PITIDO_CADA_MS + 100));
    VERIFICAR(alarma_silenciar(&a, t));
    VERIFICAR(!alarma_buzzer(&a, 0) && !alarma_buzzer(&a, ALARMA_PITIDO_CADA_MS)); /* silenciada: callada */

    printf("ok\n");
    return 0;
}
