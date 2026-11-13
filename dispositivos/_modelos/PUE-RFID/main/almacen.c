#include "almacen.h"
#include "puerta_logica.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "ALMACEN";
static nvs_handle_t h;
static bool abierto;

bool almacen_iniciar(void)
{
    esp_err_t e = nvs_open("puerta", NVS_READWRITE, &h);
    abierto = (e == ESP_OK);
    if (!abierto) ESP_LOGE(TAG, "No abre NVS: %s", esp_err_to_name(e));
    return abierto;
}

/* Escribe un blob solo si es distinto de lo que ya hay */
static bool blob_si_cambia(const char *clave, const void *datos, size_t n)
{
    if (!abierto) return false;
    size_t actual = 0;
    if (nvs_get_blob(h, clave, NULL, &actual) == ESP_OK && actual == n) {
        uint8_t viejo[sizeof(tarjeta_t) * LISTA_MAX];
        if (n <= sizeof(viejo) && nvs_get_blob(h, clave, viejo, &actual) == ESP_OK && memcmp(viejo, datos, n) == 0) {
            return true;                       /* igual: no se escribe */
        }
    }
    return nvs_set_blob(h, clave, datos, n) == ESP_OK && nvs_commit(h) == ESP_OK;
}

void almacen_cargar_lista(lista_t *l)
{
    lista_vaciar(l);
    if (!abierto) return;
    uint8_t buf[sizeof(tarjeta_t) * LISTA_MAX];
    size_t n = sizeof(buf);
    if (nvs_get_blob(h, "tarjetas", buf, &n) != ESP_OK) return;
    /* Cada elemento se vuelve a validar: una entrada corrupta no entra a la lista */
    for (size_t i = 0; i + sizeof(tarjeta_t) <= n; i += sizeof(tarjeta_t)) {
        const tarjeta_t *t = (const tarjeta_t *)(buf + i);
        if (lista_agregar(l, t->uid, t->largo) != LISTA_OK) {
            ESP_LOGW(TAG, "Llavero descartado de NVS (invalido o repetido)");
        }
    }
}

bool almacen_guardar_lista(const lista_t *l)
{
    return blob_si_cambia("tarjetas", l->t, sizeof(tarjeta_t) * l->n);
}

bool almacen_cargar_armada(void)
{
    uint8_t v = 0;
    if (abierto) nvs_get_u8(h, "armada", &v);
    return v != 0;
}

bool almacen_guardar_armada(bool armada)
{
    if (!abierto) return false;
    uint8_t actual;
    if (nvs_get_u8(h, "armada", &actual) == ESP_OK && actual == (armada ? 1 : 0)) return true;
    return nvs_set_u8(h, "armada", armada ? 1 : 0) == ESP_OK && nvs_commit(h) == ESP_OK;
}

/* Lo que se guarda de la config. Todos los campos de 4 bytes: sin relleno. */
#define CONFIG_BLOB_VERSION 1
typedef struct {
    uint32_t version;
    int32_t pulso_ms;
    int32_t aviso_s;
    uint32_t sensor;
} config_blob_t;

void almacen_cargar_config(config_puerta_t *c)
{
    c->pulso_ms = PULSO_DEFECTO_MS;
    c->aviso_s = AVISO_DEFECTO_S;
    c->sensor = SENSOR_REED;
    if (!abierto) return;
    config_blob_t b;
    size_t n = sizeof(b);
    if (nvs_get_blob(h, "config", &b, &n) != ESP_OK || n != sizeof(b) || b.version != CONFIG_BLOB_VERSION) return;
    /* Se vuelve a validar contra el rango: si algo esta fuera, no se usa nada de lo guardado */
    if (b.pulso_ms < PULSO_MINIMO_MS || b.pulso_ms > PULSO_TOPE_MS) return;
    if (b.aviso_s < AVISO_MIN_S || b.aviso_s > AVISO_MAX_S) return;
    if (b.sensor > (uint32_t)SENSOR_BOTON) return;
    c->pulso_ms = b.pulso_ms;
    c->aviso_s = b.aviso_s;
    c->sensor = (sensor_tipo_t)b.sensor;
}

bool almacen_guardar_config(const config_puerta_t *c)
{
    config_blob_t b;
    memset(&b, 0, sizeof(b));
    b.version = CONFIG_BLOB_VERSION;
    b.pulso_ms = c->pulso_ms;
    b.aviso_s = c->aviso_s;
    b.sensor = (uint32_t)c->sensor;
    return blob_si_cambia("config", &b, sizeof(b));
}
