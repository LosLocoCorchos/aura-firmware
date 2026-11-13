#include "configuracion.h"
#include "almacenamiento.h"
#include "aura_trama.h"
#include "nvs.h"
#include "energia.h"
#include "driver/gpio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* NVS transaccional en memoria: un commit fallido no cambia el dato durable. */
typedef struct { unsigned char datos[64]; size_t tam; } registro_t;
static registro_t registros[2];
static registro_t pendiente;
static unsigned clave_pendiente;
static bool fallar_escritura;
static bool fallar_commit;
static unsigned escrituras;
static int nivel_red = 1;
static int64_t reloj_us;
static unsigned alertas;

esp_err_t gpio_config(const gpio_config_t *cfg) { (void)cfg; return ESP_OK; }
int gpio_get_level(int pin) { assert(pin == 1); return nivel_red; }
int64_t esp_timer_get_time(void) { return reloj_us; }
void nodo_mesh_alerta(const char *tipo, const char *severity, const char *message, const cJSON *details)
{
    (void)severity;
    (void)message;
    assert(strcmp(tipo, "energia") == 0);
    assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(details, "alimentacion")));
    ++alertas;
}

static unsigned indice(const char *clave)
{
    if (strcmp(clave, "config") == 0) return 0;
    assert(strcmp(clave, "bateria") == 0);
    return 1;
}

const char *esp_err_to_name(esp_err_t err) { (void)err; return "doble"; }
esp_err_t nvs_open(const char *nombre, int modo, nvs_handle_t *h)
{
    assert(strcmp(nombre, "hfr_idf") == 0);
    (void)modo;
    *h = 1;
    return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *clave, void *datos, size_t *tam)
{
    (void)h;
    registro_t *r = &registros[indice(clave)];
    if (r->tam == 0) return ESP_ERR_NVS_NOT_FOUND;
    if (*tam < r->tam) return ESP_ERR_INVALID_SIZE;
    memcpy(datos, r->datos, r->tam);
    *tam = r->tam;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *clave, const void *datos, size_t tam)
{
    (void)h;
    ++escrituras;
    if (fallar_escritura) return ESP_FAIL;
    assert(tam <= sizeof(pendiente.datos));
    memcpy(pendiente.datos, datos, tam);
    pendiente.tam = tam;
    clave_pendiente = indice(clave);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (fallar_commit) return ESP_FAIL;
    registros[clave_pendiente] = pendiente;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }

static void configuracion(void)
{
    assert(hfr_config_iniciar(true) == ESP_OK);
    hfr_config_t cfg = hfr_config_actual();
    assert(cfg.intervalo_s == 60 && cfg.freezer_s == 300);
    const char *invalidos[] = {
        "{}", "[]", "null", "{\"intervalo_s\":4}", "{\"f_s\":86401}",
        "{\"intervalo_s\":5.5}", "{\"f_s\":true}", "{\"hlo\":\"2\"}",
        "{\"hlo\":-55.01}", "{\"hhi\":125.01}", "{\"flo\":-20.001}",
        "{\"hlo\":6}", "{\"flo\":-10,\"fhi\":-15}",
        "{\"f_s\":5,\"desconocido\":0}", "{\"r_s\":300}",
        "{\"f_s\":5,\"f_s\":6}", "{\"intervalo_s\":1e300}",
    };
    char motivo[80];
    for (size_t i = 0; i < sizeof(invalidos) / sizeof(*invalidos); ++i) {
        cJSON *params = cJSON_Parse(invalidos[i]);
        assert(!hfr_config_aplicar(params, motivo, sizeof(motivo)));
        assert(motivo[0] != 0);
        hfr_config_t despues = hfr_config_actual();
        assert(memcmp(&cfg, &despues, sizeof(cfg)) == 0);
        assert(escrituras == 0);
        cJSON_Delete(params);
    }
    cJSON *precision = cJSON_Parse("{\"flo\":-20.001}");
    assert(!hfr_config_aplicar(precision, motivo, sizeof(motivo)));
    assert(strcmp(motivo, "umbral_mas_de_dos_decimales") == 0);
    cJSON_Delete(precision);
    cJSON *params = cJSON_Parse("{\"intervalo_s\":5,\"hlo\":-55,\"hhi\":125,\"aula\":\"aula1\"}");
    assert(hfr_config_aplicar(params, motivo, sizeof(motivo)));
    assert(escrituras == 1);
    cfg = hfr_config_actual();
    assert(cfg.intervalo_s == 5 && cfg.freezer_s == 300 && cfg.minimo[0] == -5500);
    assert(hfr_config_aplicar(params, motivo, sizeof(motivo)));
    assert(escrituras == 1); /* No desgastar flash si no cambió. */
    cJSON_Delete(params);
    params = cJSON_Parse("{\"f_s\":86400,\"flo\":-25.25,\"fhi\":-14.75}");
    fallar_escritura = true;
    assert(!hfr_config_aplicar(params, motivo, sizeof(motivo)));
    fallar_escritura = false;
    fallar_commit = true;
    assert(!hfr_config_aplicar(params, motivo, sizeof(motivo)));
    fallar_commit = false;
    hfr_config_t despues = hfr_config_actual();
    assert(memcmp(&cfg, &despues, sizeof(cfg)) == 0);
    hfr_config_t durable;
    assert(hfr_cargar_config(&durable) == ESP_OK);
    assert(memcmp(&cfg, &durable, sizeof(cfg)) == 0);
    assert(hfr_config_aplicar(params, motivo, sizeof(motivo)));
    cJSON_Delete(params);
    cfg = hfr_config_actual();
    assert(cfg.freezer_s == 86400 && cfg.minimo[1] == -2525 && cfg.maximo[1] == -1475);
    assert(hfr_cargar_config(&durable) == ESP_OK);
    assert(memcmp(&cfg, &durable, sizeof(cfg)) == 0);
    cJSON *reporte = cJSON_CreateObject();
    hfr_config_describir(reporte);
    assert(cJSON_GetArraySize(reporte) == 6);
    assert(cJSON_GetObjectItemCaseSensitive(reporte, "f_s")->valuedouble == 86400);
    cJSON_Delete(reporte);
    registros[0].datos[7] ^= 1;
    assert(hfr_cargar_config(&durable) == ESP_ERR_INVALID_CRC);
    puts("OK configuracion: 17 parches invalidos, atomicidad, limites, NVS y CRC");
}

static void sondas(void)
{
    float invalidas[] = {NAN, INFINITY, -INFINITY, -127, 85, -55.1f, 125.1f};
    for (size_t i = 0; i < sizeof(invalidas) / sizeof(*invalidas); ++i) {
        assert(hfr_clasificar(invalidas[i], true) != HFR_SONDA_OK);
    }
    assert(hfr_clasificar(4, false) == HFR_SIN_RESPUESTA);
    assert(hfr_clasificar(-55, true) == HFR_SONDA_OK);
    assert(hfr_clasificar(125, true) == HFR_SONDA_OK);
    puts("OK sondas: centinelas y limites");
}

static void alarma(void)
{
    hfr_alarma_t heladera = {0};
    hfr_alarma_t freezer = {0};
    for (unsigned i = 0; i < 5; ++i) {
        hfr_alarma_actualizar(&heladera, 6, 200, 600);
        hfr_alarma_actualizar(&heladera, 6.5f, 200, 600);
        assert(heladera.activa == 0);
    }
    hfr_alarma_actualizar(&heladera, 6.5f, 200, 600);
    assert(heladera.activa == 1 && freezer.activa == 0);
    hfr_alarma_actualizar(&heladera, 6, 200, 600);
    assert(heladera.activa == 1);
    hfr_alarma_actualizar(&heladera, 5.5f, 200, 600);
    assert(heladera.activa == 0);

    hfr_alarma_actualizar(&freezer, -26, -2500, -1500);
    assert(freezer.activa == 0);
    hfr_alarma_actualizar(&freezer, -26, -2500, -1500);
    assert(freezer.activa == -1);
    hfr_alarma_actualizar(&freezer, -25, -2500, -1500);
    assert(freezer.activa == -1);
    hfr_alarma_actualizar(&freezer, -24.5f, -2500, -1500);
    assert(freezer.activa == 0);
    puts("OK alarma: demora, oscilacion, recuperacion y sondas independientes");
}

static void reloj(void)
{
    const uint32_t fechas[] = {AURA_EPOCA_MINIMA, 1792490400, 4102444799UL};
    for (size_t i = 0; i < sizeof(fechas) / sizeof(*fechas); ++i) {
        uint8_t r[7];
        uint32_t utc;
        assert(hfr_rtc_codificar(fechas[i], r));
        assert(hfr_rtc_decodificar(r, &utc) && utc == fechas[i]);
    }
    uint8_t r[7] = {0,0,0,5,0x29,0x02,0x28};
    uint32_t utc;
    assert(hfr_rtc_decodificar(r, &utc) && utc == 1835395200);
    r[6] = 0x27; /* 29/2/2027 no existe. */
    assert(!hfr_rtc_decodificar(r, &utc));
    r[6] = 0x28;
    r[2] = 0x52; /* 12 AM. */
    assert(hfr_rtc_decodificar(r, &utc) && utc == 1835395200);
    r[2] = 0x72; /* 12 PM. */
    assert(hfr_rtc_decodificar(r, &utc) && utc == 1835438400);
    r[0] = 0x6a;
    assert(!hfr_rtc_decodificar(r, &utc));
    assert(!hfr_rtc_codificar(AURA_EPOCA_MINIMA - 1, r));
    puts("OK RTC: UTC, bisiestos, 12/24 horas y BCD invalido");
}

static void energia(void)
{
    memset(registros, 0, sizeof(registros));
    reloj_us = 2000000;
    nivel_red = 1;
    hfr_energia_iniciar(true);
    reloj_us += 200001;
    hfr_energia_actualizar(true);
    assert(!hfr_energia_bateria() && alertas == 0);

    nivel_red = 0;
    hfr_energia_actualizar(true);
    reloj_us += 100000;
    nivel_red = 1; /* Rebote, no es un corte. */
    hfr_energia_actualizar(true);
    assert(!hfr_energia_bateria() && alertas == 0);

    nivel_red = 0;
    hfr_energia_actualizar(true);
    reloj_us += 200001;
    hfr_energia_actualizar(true);
    assert(hfr_energia_bateria() && alertas == 1);

    hfr_energia_iniciar(true);
    reloj_us += 200001;
    hfr_energia_actualizar(true);
    assert(hfr_energia_bateria() && alertas == 1);

    nivel_red = 1;
    hfr_energia_actualizar(true);
    reloj_us += 200001;
    hfr_energia_actualizar(true);
    assert(!hfr_energia_bateria() && alertas == 2);

    nivel_red = 0;
    hfr_energia_actualizar(true);
    reloj_us += 200001;
    hfr_energia_actualizar(true);
    assert(hfr_energia_bateria() && alertas == 3);
    puts("OK energia: rebotes, reinicio y alertas para cortes sucesivos");
}

int main(void)
{
    assert(hfr_crc32("123456789", 9) == 0xcbf43926);
    configuracion();
    sondas();
    alarma();
    reloj();
    energia();
    puts("Todas las pruebas de host del nodo pasaron");
    return 0;
}
