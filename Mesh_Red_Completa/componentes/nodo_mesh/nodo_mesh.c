/*
 * nodo_mesh: hoja de la mesh de AURA. Ver include/nodo_mesh.h.
 *
 * Tareas:
 *  - mesh_rx:  recibe con esp_mesh_recv, valida y encola. Nunca envía.
 *  - nodo:     atiende lo recibido y envía (eventos, muestras, pedido de hora).
 *  - vigia:    busca padre cuando no hay, y en plan B revisa si volvió el concentrador.
 *  - ota:      (aura_ota) recibe una versión nueva del firmware por la malla.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "nvs.h"
#include "aura_trama.h"
#include "aura_util.h"
#include "aura_ota.h"
#include "aura_fw.h"
#include "aura_ota_receptor.h"
#include "cola.h"
#include "nodo_mesh.h"

#define MESH_MAX_CAPAS          6
/* La API de la malla exige un SSID de router, pero la hoja nunca se conecta al router:
   solo el gateway depende del WiFi. Es un valor fijo que NO hay que cambiar. */
#define ROUTER_FICTICIO         "aura-sin-router"

#define ESPERA_CONEXION_MS      15000   /* sin conectar tras elegir padre -> reescanear */
#define ESPERA_RECONEXION_MS    5000    /* tras perder el padre, cuánto se le deja volver solo */
#define BUSQUEDAS_PLAN_B        3       /* escaneos sin ver al concentrador -> plan B */
#define REVISAR_AULA_MS         30000   /* en plan B, cada cuánto ver si volvió */
#define LIMITE_ENVIO_MS         5000    /* tiempo máximo de un esp_mesh_send() */
#define FALLOS_PARA_RECONECTAR  3       /* envíos fallidos seguidos -> reconexión forzada */
#define BEACON_HOJA_TU          60000   /* intervalo de beacon del AP de la hoja: el máximo */
#define PEDIR_HORA_MS           10000   /* sin hora: cada cuánto pedirla */
#define ESCUCHA_CANAL_MS        1500    /* escaneo del canal del último padre: más largo que uno común */
#define REINTENTO_SALTO_MS      2000    /* la mesh no aceptó la muestra: reintento corto */
#define EVENTOS_CAP             8       /* resultados, reportes y alertas esperando confirmación */
#define EVENTO_ESPERA_MIN_MS    2000    /* sin EVENTO_OK: 2 s, 4 s, 8 s... hasta 30 s (con azar) */
#define EVENTO_ESPERA_MAX_MS    30000
#define NVS_ESPACIO             "nodo_mesh"
#define ESPERA_EVENTOS_MS       10000   /* antes de reiniciarse por una actualización */

typedef enum { MODO_AULA, MODO_DIRECTO } modo_t;

static const char *TAG = "NODO_MESH";
static nodo_mesh_config_t cfg;

/* ---------- Estado de la conexión ---------- */
static volatile bool conectado = false;
static volatile bool reconecto = false;          /* lo consume la tarea nodo: manda el reporte */
static volatile bool escaneando = false;
static volatile TickType_t ultimo_intento = 0;
static volatile TickType_t ultima_revision = 0;
static volatile TickType_t desconectada_desde = 0;
static volatile bool reintentando = false;
static volatile modo_t modo = MODO_DIRECTO;
static volatile modo_t modo_pedido = MODO_DIRECTO;
static int busquedas_fallidas = 0;
/* Canal del último padre (NVS, se escribe solo si cambia). Al arrancar y al perder el padre se
   escanea primero ese canal: la hoja vuelve en ~4 s en lugar de escanear los 13 canales (8 s cada
   pasada, y a veces el concentrador no aparecía en la primera: 10 a 27 s tras un reinicio). */
static uint8_t canal_conocido = 0;
static volatile bool probar_canal = true;
static volatile bool escaneo_de_canal = false;
static volatile int fallos_envio = 0;
static volatile int revisiones_rapidas = 0;      /* tras cambiar el aula: buscar al concentrador ya */
static mesh_addr_t concentrador, concentrador_pedido;
static uint8_t bssid_padre[6];                    /* AP del padre actual */

static char aula[AURA_AULA_MAX] = "";
static uint8_t mi_mac[6];

/* Concentradores vistos en los beacons: BSSID del AP -> MAC en la malla y si es de mi aula */
#define MAX_CANDIDATOS 8
typedef struct {
    uint8_t bssid[6];
    uint8_t mac_malla[6];
    bool de_mi_aula;
} candidato_t;
static candidato_t candidatos[MAX_CANDIDATOS];
static int cant_candidatos = 0;
static portMUX_TYPE lock_candidatos = portMUX_INITIALIZER_UNLOCKED;

/* ---------- Recepción ---------- */
typedef struct {
    uint8_t de[6];
    uint16_t largo;
    uint8_t datos[AURA_TRAMA_MAX];
} recibida_t;
static QueueHandle_t cola_rx = NULL;
static uint32_t descartadas_rx = 0;

/* ---------- Envío ---------- */
static uint16_t seq = 0;
static portMUX_TYPE lock_seq = portMUX_INITIALIZER_UNLOCKED;
/* Eventos (resultado, reporte, alerta): de a uno, hasta que la raíz confirma que los
   publicó (EVENTO_OK). Así no se pierden si se cae el broker o el WiFi. */
static SemaphoreHandle_t lock_eventos = NULL;
static aura_trama_t eventos[EVENTOS_CAP];
static int eventos_cabeza = 0, eventos_cant = 0;
static bool evento_en_vuelo = false;
static TickType_t evento_enviado_en = 0, evento_espera = 0;
static uint32_t eventos_perdidos = 0;
/* Muestras: de a una, la más vieja, hasta la CONFIRMACIÓN de AURA */
static bool en_vuelo = false;
static uint8_t id_en_vuelo[AURA_INGEST_ID_BYTES];
static TickType_t enviada_en = 0, proximo_envio = 0, espera_muestra = 0;
static uint32_t intentos = 0;
static bool hay_ultimo_cmd = false;
static uint8_t ultimo_cmd_de[6];
static uint16_t ultimo_cmd_seq = 0;

/* ---------- Último estado, para el concentrador (pantalla y mando local) ---------- */
/* La cola guarda el historial para AURA; el concentrador solo necesita lo último y en el acto. */
static portMUX_TYPE lock_estado = portMUX_INITIALIZER_UNLOCKED;
static char ultimo_values[AURA_VALUES_MAX];
static uint8_t ultimo_largo = 0;
static uint32_t ultimo_ts = 0, ultimo_uptime = 0;
static volatile bool estado_pendiente = false;   /* mandar el ESTADO al concentrador */

/* ---------- Sondas: una alerta por cambio ---------- */
#define SONDAS_MAX 8
static struct { char campo[24]; bool falla; } sondas[SONDAS_MAX];
static int cant_sondas = 0;

/* ================================================================== */
/*  Hora                                                               */
/* ================================================================== */
static uint32_t ahora_epoca(void)
{
    time_t t = time(NULL);
    return (t > 0 && aura_hora_valida((uint32_t)t)) ? (uint32_t)t : 0;
}

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

static void hora_ajustar_ms(uint64_t ms)
{
    if (!aura_hora_valida((uint32_t)(ms / 1000))) {
        return;
    }
    bool primera = ahora_epoca() == 0;
    struct timeval tv = {.tv_sec = (time_t)(ms / 1000), .tv_usec = (suseconds_t)(ms % 1000) * 1000};
    settimeofday(&tv, NULL);
    if (primera) {
        ESP_LOGI(TAG, "Hora recibida: las mediciones ya salen con ts");
    }
}

/* ================================================================== */
/*  NVS                                                                */
/* ================================================================== */
/* Solo lectura: la NVS se escribe únicamente cuando AURA cambia el aula */
static void nvs_cargar(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_ESPACIO, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof(aula);
    if (nvs_get_str(h, "aula", aula, &n) != ESP_OK || !aura_aula_valida(aula)) {
        aula[0] = '\0';
    }
    if (nvs_get_u8(h, "canal", &canal_conocido) != ESP_OK || canal_conocido > 13) {
        canal_conocido = 0;
    }
    nvs_close(h);
}

static void canal_guardar(uint8_t canal)
{
    if (canal == 0 || canal == canal_conocido) {
        return;
    }
    canal_conocido = canal;
    nvs_handle_t h;
    if (nvs_open(NVS_ESPACIO, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "canal", canal);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint16_t siguiente_seq(void)
{
    taskENTER_CRITICAL(&lock_seq);
    uint16_t s = seq++;
    taskEXIT_CRITICAL(&lock_seq);
    return s;
}

static bool nvs_guardar_aula(const char *nueva)
{
    nvs_handle_t h;
    if (nvs_open(NVS_ESPACIO, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    bool ok = nvs_set_str(h, "aula", nueva) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

/* ================================================================== */
/*  Envío por la malla                                                 */
/* ================================================================== */
/* Al concentrador en modo aula; a la raíz en plan B o sin aula */
static bool enviar(const aura_trama_t *t)
{
    if (!conectado) {
        return false;
    }
    bool via_aula = (modo == MODO_AULA);
    mesh_data_t d = {
        .data = (uint8_t *)t,
        .size = aura_trama_bytes(t),
        .proto = MESH_PROTO_BIN,
        .tos = MESH_TOS_P2P,
    };
    esp_err_t err = esp_mesh_send(via_aula ? &concentrador : NULL, &d, MESH_DATA_P2P, NULL, 0);
    if (err == ESP_OK) {
        fallos_envio = 0;
        return true;
    }
    ESP_LOGW(TAG, "Error al enviar a %s: %s", via_aula ? "concentrador" : "raíz", esp_err_to_name(err));
    /* Vigilante de envío: si el padre se reinició muy rápido, la hoja puede quedar
       "conectada" con el control de flujo trabado ([WND-RX] timeout). Se fuerza la
       desconexión y la búsqueda normal de padre. */
    if (conectado && ++fallos_envio >= FALLOS_PARA_RECONECTAR) {
        ESP_LOGW(TAG, "%d envíos fallidos seguidos: fuerzo la reconexión", fallos_envio);
        fallos_envio = 0;
        esp_mesh_disconnect();
    }
    return false;
}

static void evento_encolar(uint8_t tipo, const cJSON *doc)
{
    char *txt = cJSON_PrintUnformatted(doc);
    if (txt == NULL) {
        return;
    }
    size_t n = strlen(txt);
    if (n > AURA_PAYLOAD_MAX) {
        ESP_LOGW(TAG, "Evento tipo %u de %u bytes (máximo %u): no se envía", tipo, (unsigned)n, AURA_PAYLOAD_MAX);
        cJSON_free(txt);
        return;
    }
    xSemaphoreTake(lock_eventos, portMAX_DELAY);
    /* Un reporte nuevo reemplaza a uno viejo que todavía no salió: solo importa el estado actual */
    bool reemplazado = false;
    for (int i = evento_en_vuelo ? 1 : 0; tipo == AURA_TIPO_REPORTE && i < eventos_cant; i++) {
        aura_trama_t *e = &eventos[(eventos_cabeza + i) % EVENTOS_CAP];
        if (e->tipo == AURA_TIPO_REPORTE) {
            aura_trama_init(e, tipo, siguiente_seq(), txt, n);
            reemplazado = true;
            break;
        }
    }
    if (!reemplazado) {
        if (eventos_cant == EVENTOS_CAP) {             /* lleno: se pierde el más viejo */
            eventos_cabeza = (eventos_cabeza + 1) % EVENTOS_CAP;
            eventos_cant--;
            evento_en_vuelo = false;
            eventos_perdidos++;
        }
        aura_trama_init(&eventos[(eventos_cabeza + eventos_cant) % EVENTOS_CAP], tipo, siguiente_seq(), txt, n);
        eventos_cant++;
    }
    xSemaphoreGive(lock_eventos);
    cJSON_free(txt);
}

/* De a uno: el más viejo, hasta que llegue su EVENTO_OK (espera creciente, con azar) */
/* El último valor medido, directo al concentrador (no pasa por la cola ni sube a la raíz) */
static void enviar_estado(void)
{
    if (!estado_pendiente || modo != MODO_AULA) {
        estado_pendiente = false;              /* sin concentrador no hay pantalla que actualizar */
        return;
    }
    uint8_t p[AURA_PAYLOAD_MAX];
    taskENTER_CRITICAL(&lock_estado);
    uint8_t n = ultimo_largo;
    uint32_t ts = ultimo_ts, up = ultimo_uptime;
    memcpy(p + 4, ultimo_values, n);
    taskEXIT_CRITICAL(&lock_estado);
    if (n == 0) {
        return;                                /* todavía no midió: sale con la primera medición */
    }
    uint32_t hoy = ahora_epoca();
    if (ts == 0 && hoy != 0) {
        ts = hoy - (uptime_s() - up);
    }
    aura_u32_escribir(p, ts);
    aura_trama_t t;
    aura_trama_init(&t, AURA_TIPO_ESTADO, siguiente_seq(), p, 4 + n);
    if (enviar(&t)) {
        estado_pendiente = false;
    }
}

static void enviar_evento(void)
{
    aura_trama_t t;
    TickType_t ahora = xTaskGetTickCount();
    xSemaphoreTake(lock_eventos, portMAX_DELAY);
    bool toca = eventos_cant > 0 && (!evento_en_vuelo || (ahora - evento_enviado_en) >= evento_espera);
    bool reintento = evento_en_vuelo;
    uint32_t anterior_ms = pdTICKS_TO_MS(evento_espera);
    if (toca) {
        t = eventos[eventos_cabeza];
    }
    xSemaphoreGive(lock_eventos);
    if (!toca) {
        return;
    }
    bool ok = enviar(&t);
    uint32_t base = EVENTO_ESPERA_MIN_MS;
    if (ok && reintento) {
        base = anterior_ms * 2 > EVENTO_ESPERA_MAX_MS ? EVENTO_ESPERA_MAX_MS : anterior_ms * 2;
    }
    xSemaphoreTake(lock_eventos, portMAX_DELAY);
    if (eventos_cant > 0 && eventos[eventos_cabeza].seq == t.seq) {
        evento_en_vuelo = true;
        evento_enviado_en = ahora;
        evento_espera = pdMS_TO_TICKS(aura_con_azar(base, esp_random()));
    }
    xSemaphoreGive(lock_eventos);
}

static void evento_confirmado(uint16_t s)
{
    xSemaphoreTake(lock_eventos, portMAX_DELAY);
    if (eventos_cant > 0 && eventos[eventos_cabeza].seq == s) {
        eventos_cabeza = (eventos_cabeza + 1) % EVENTOS_CAP;
        eventos_cant--;
        evento_en_vuelo = false;
    }
    xSemaphoreGive(lock_eventos);
}

/* ================================================================== */
/*  Reporte, alertas, resultado                                        */
/* ================================================================== */
static void describir(cJSON *config)
{
    if (aula[0]) {
        cJSON_AddStringToObject(config, "aula", aula);
    } else {
        cJSON_AddNullToObject(config, "aula");
    }
    if (cfg.describir_config) {
        cfg.describir_config(config);
    }
}

void nodo_mesh_reportar(void)
{
    cJSON *d = cJSON_CreateObject();
    char fw[2 * AURA_OTA_TEXTO];
    aura_fw_texto(fw, sizeof(fw));
    cJSON_AddStringToObject(d, "fw", fw);              /* tipo@versión: la raíz lo publica como firmware */
    describir(cJSON_AddObjectToObject(d, "config"));
    cJSON_AddNumberToObject(d, "pendientes", cola_cantidad());
    cJSON_AddNumberToObject(d, "descartadas", cola_descartadas());
    if (cola_en_flash()) {
        cJSON_AddNumberToObject(d, "en_flash", cola_en_flash());
    }
    if (eventos_perdidos) {
        cJSON_AddNumberToObject(d, "eventos_perdidos", eventos_perdidos);
    }
    cJSON_AddStringToObject(d, "alimentacion", cfg.alimentacion ? cfg.alimentacion : "desconocida");
    cJSON_AddStringToObject(d, "modo", aula[0] == '\0' ? "sin_aula" : modo == MODO_AULA ? "aula" : "directo");
    /* Enlace: van en status y no en values (contrato §3.1). rssi = cómo recibe a su padre. */
    wifi_ap_record_t padre;
    if (conectado && esp_wifi_sta_get_ap_info(&padre) == ESP_OK) {
        cJSON_AddNumberToObject(d, "rssi", padre.rssi);
        cJSON_AddNumberToObject(d, "capa", esp_mesh_get_layer());
    }
    /* La trama tiene 180 B: si no entra, se sacan primero los datos accesorios (nunca fw ni config) */
    static const char *const accesorios[] = {"descartadas", "rssi", "capa", "alimentacion", "modo", "pendientes"};
    for (int i = 0; i < (int)(sizeof(accesorios) / sizeof(accesorios[0])); i++) {
        char *txt = cJSON_PrintUnformatted(d);
        size_t n = txt ? strlen(txt) : 0;
        cJSON_free(txt);
        if (n <= AURA_PAYLOAD_MAX) {
            break;
        }
        cJSON_DeleteItemFromObject(d, accesorios[i]);
    }
    evento_encolar(AURA_TIPO_REPORTE, d);
    cJSON_Delete(d);
}

void nodo_mesh_alerta(const char *tipo, const char *severity, const char *message, const cJSON *details)
{
    cJSON *d = cJSON_CreateObject();
    cJSON_AddStringToObject(d, "tipo", tipo);
    cJSON_AddStringToObject(d, "severity", severity);
    cJSON_AddStringToObject(d, "message", message);
    if (details) {
        cJSON_AddItemToObject(d, "details", cJSON_Duplicate(details, true));
    }
    evento_encolar(AURA_TIPO_ALERTA, d);
    cJSON_Delete(d);
}

void nodo_mesh_sonda(const char *campo, bool ok, const char *motivo_falla)
{
    int i;
    for (i = 0; i < cant_sondas && strcmp(sondas[i].campo, campo) != 0; i++) {
    }
    bool cambio;
    if (i < cant_sondas) {
        cambio = sondas[i].falla == ok;
        sondas[i].falla = !ok;
    } else {
        if (cant_sondas == SONDAS_MAX || strlen(campo) >= sizeof(sondas[0].campo)) {
            return;
        }
        strcpy(sondas[cant_sondas].campo, campo);
        sondas[cant_sondas++].falla = !ok;
        cambio = !ok;                                  /* la primera vez solo avisa si falla */
    }
    if (!cambio) {
        return;
    }
    cJSON *det = cJSON_CreateObject();
    cJSON_AddStringToObject(det, "campo", campo);
    cJSON_AddStringToObject(det, "motivo", ok ? "recuperada" : (motivo_falla ? motivo_falla : "sin_respuesta"));
    char msg[64];
    snprintf(msg, sizeof(msg), ok ? "Sonda %s recuperada" : "Sonda %s con falla", campo);
    nodo_mesh_alerta("sensor", ok ? "info" : "high", msg, det);
    cJSON_Delete(det);
}

static void resultado(const char *command_id, bool aplicado, const char *motivo)
{
    cJSON *d = cJSON_CreateObject();
    if (command_id) {
        cJSON_AddStringToObject(d, "command_id", command_id);
    }
    cJSON_AddBoolToObject(d, "aplicado", aplicado);
    if (motivo && motivo[0]) {
        cJSON_AddStringToObject(d, "motivo", motivo);
    }
    cJSON *config = cJSON_AddObjectToObject(d, "config");
    describir(config);
    char *txt = cJSON_PrintUnformatted(d);
    if (txt && strlen(txt) > AURA_PAYLOAD_MAX) {
        cJSON_DeleteItemFromObject(d, "config");       /* sin config antes que sin resultado */
    }
    cJSON_free(txt);
    evento_encolar(AURA_TIPO_RESULTADO, d);
    cJSON_Delete(d);
}

/* ================================================================== */
/*  Mediciones                                                         */
/* ================================================================== */
bool nodo_mesh_medicion(const cJSON *values)
{
    if (!cJSON_IsObject(values) || cJSON_GetArraySize(values) == 0) {
        return false;
    }
    char *txt = cJSON_PrintUnformatted(values);
    if (txt == NULL) {
        return false;
    }
    size_t n = strlen(txt);
    if (n > AURA_VALUES_MAX) {
        ESP_LOGW(TAG, "values de %u bytes, el máximo es %u", (unsigned)n, AURA_VALUES_MAX);
        cJSON_free(txt);
        return false;
    }
    cola_muestra_t m = {0};
    aura_ingest_id_nuevo(m.ingest_id, esp_random);
    m.ts = ahora_epoca();
    m.uptime_s = uptime_s();
    m.largo = (uint8_t)n;
    memcpy(m.values, txt, n);
    bool ok = cola_guardar(&m);
    taskENTER_CRITICAL(&lock_estado);
    memcpy(ultimo_values, txt, n);
    ultimo_largo = (uint8_t)n;
    ultimo_ts = m.ts;
    ultimo_uptime = m.uptime_s;
    taskEXIT_CRITICAL(&lock_estado);
    /* Si hay cola atrás (AURA o la red de arriba caída), esta medición no va a pasar por el
       concentrador enseguida: se le manda el estado aparte. Con la cola al día no hace falta. */
    if (cola_cantidad() > 1) {
        estado_pendiente = true;
    }
    if (ok) {
        ESP_LOGI(TAG, "Medición guardada %s, en cola %lu", txt, (unsigned long)cola_cantidad());
    } else {
        ESP_LOGW(TAG, "No se pudo guardar la medición");
    }
    cJSON_free(txt);
    return ok;
}

/* Una sola muestra en vuelo, la más vieja. Se reenvía si AURA no la confirma a tiempo. */
static void enviar_muestra(void)
{
    TickType_t ahora = xTaskGetTickCount();
    if ((int32_t)(ahora - proximo_envio) < 0) {
        return;
    }
    cola_muestra_t m;
    if (!cola_mirar(&m)) {
        en_vuelo = false;
        return;
    }
    bool misma = en_vuelo && memcmp(m.ingest_id, id_en_vuelo, AURA_INGEST_ID_BYTES) == 0;
    if (misma && (ahora - enviada_en) < espera_muestra) {
        return;
    }
    intentos = misma ? intentos + 1 : 0;

    /* Sin hora al medir: ahora que hay hora, se fecha con el uptime (si es de este arranque) */
    uint32_t ts = m.ts;
    uint32_t hoy = ahora_epoca();
    if (ts == 0 && hoy != 0 && !m.otro_arranque) {
        ts = hoy - (uptime_s() - m.uptime_s);
    }
    aura_trama_t t;
    aura_telemetria_armar(&t, siguiente_seq(), m.ingest_id, ts, m.values, m.largo);
    if (enviar(&t)) {
        en_vuelo = true;
        memcpy(id_en_vuelo, m.ingest_id, AURA_INGEST_ID_BYTES);
        enviada_en = ahora;
        /* 15 s, 30 s, 1 min... hasta 5 min, con azar: tras un corte no reintentan todas juntas */
        espera_muestra = pdMS_TO_TICKS(aura_con_azar(aura_espera_confirmacion_ms(intentos), esp_random()));
        if (intentos) {
            ESP_LOGI(TAG, "Reintento %lu de la muestra más vieja (sin confirmar)", (unsigned long)intentos);
        }
    } else {
        en_vuelo = false;
        proximo_envio = ahora + pdMS_TO_TICKS(REINTENTO_SALTO_MS);
    }
}

uint32_t nodo_mesh_pendientes(void)
{
    return cola_cantidad();
}

bool nodo_mesh_conectada(void)
{
    return conectado;
}

const char *nodo_mesh_aula(void)
{
    return aula;
}

/* ================================================================== */
/*  Lo que llega                                                       */
/* ================================================================== */
static void aula_cambiar(const char *nueva)
{
    bool cambio = strcmp(aula, nueva) != 0;
    taskENTER_CRITICAL(&lock_candidatos);
    strlcpy(aula, nueva, sizeof(aula));
    taskEXIT_CRITICAL(&lock_candidatos);
    if (!cambio) {
        return;
    }
    ESP_LOGI(TAG, "Aula asignada: %s", aula);
    if (conectado && modo == MODO_AULA) {
        esp_mesh_disconnect();                         /* estaba en otra aula: elegir de nuevo */
    } else {
        revisiones_rapidas = 5;                        /* plan B o sin aula: buscar su concentrador ya */
    }
}

static void procesar_comando(const recibida_t *r, const aura_trama_t *t)
{
    /* Una retransmisión trae el mismo seq del mismo origen: no se ejecuta dos veces */
    if (hay_ultimo_cmd && ultimo_cmd_seq == t->seq && memcmp(ultimo_cmd_de, r->de, 6) == 0) {
        return;
    }
    hay_ultimo_cmd = true;
    ultimo_cmd_seq = t->seq;
    memcpy(ultimo_cmd_de, r->de, 6);

    char json[AURA_PAYLOAD_MAX + 1];
    aura_payload_texto(t, json, sizeof(json));
    ESP_LOGI(TAG, "Comando de " MACSTR ": %s", MAC2STR(r->de), json);
    cJSON *d = cJSON_Parse(json);
    if (!cJSON_IsObject(d)) {
        resultado(NULL, false, "json_invalido");
        cJSON_Delete(d);
        return;
    }
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(d, "command");
    const cJSON *ci = cJSON_GetObjectItemCaseSensitive(d, "command_id");
    const cJSON *params = cJSON_GetObjectItemCaseSensitive(d, "params");
    const cJSON *o = cJSON_GetObjectItemCaseSensitive(d, "origen");
    const char *comando = cJSON_IsString(c) ? c->valuestring : "";
    const char *command_id = cJSON_IsString(ci) ? ci->valuestring : NULL;
    bool local = cJSON_IsString(o) && strcmp(o->valuestring, "local") == 0;
    char motivo[48] = "";

    if (params != NULL && !cJSON_IsObject(params)) {
        /* params es un objeto o no viene: con un arreglo, los hijos no tienen nombre
           (string NULL) y el strcmp de aplicar_config o ejecutar_comando hace caer la placa. */
        if (!local) {
            resultado(command_id, false, "json_invalido");
        }
        cJSON_Delete(d);
        return;
    }

    if (strcmp(comando, "set_config") == 0) {
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(params, "aula");
        bool solo_aula = a != NULL && cJSON_GetArraySize(params) == 1;
        bool ok = true;
        if (a != NULL && !(cJSON_IsString(a) && aura_aula_valida(a->valuestring))) {
            ok = false;
            strlcpy(motivo, "aula_invalida", sizeof(motivo));
        } else if (!solo_aula) {
            if (cfg.aplicar_config == NULL) {
                ok = false;
                strlcpy(motivo, "sin_parametros", sizeof(motivo));
            } else {
                ok = cfg.aplicar_config(params, motivo, sizeof(motivo));   /* valida todo o nada */
            }
        }
        if (ok && a != NULL) {
            if (nvs_guardar_aula(a->valuestring)) {
                aula_cambiar(a->valuestring);
            } else {
                ok = false;
                strlcpy(motivo, "error_de_nvs", sizeof(motivo));
            }
        }
        if (!local) {
            resultado(command_id, ok, ok ? "" : motivo);
        }
        if (ok) {
            nodo_mesh_reportar();
        }
    } else if (cfg.ejecutar_comando) {
        bool ok = cfg.ejecutar_comando(comando, params, local, motivo, sizeof(motivo));
        if (!local) {
            resultado(command_id, ok, ok ? "" : (motivo[0] ? motivo : "comando_desconocido"));
        }
    } else if (!local) {
        resultado(command_id, false, "comando_desconocido");
    }
    cJSON_Delete(d);
}

static void procesar_confirmacion(const aura_trama_t *t)
{
    if (t->largo != AURA_CONFIRMACION_BYTES) {
        return;
    }
    uint32_t hora = aura_u32_leer(t->payload + AURA_INGEST_ID_BYTES);
    uint32_t mia = ahora_epoca();
    /* La hora de la raíz: se ajusta si no había o si se corrió más de 2 s */
    if (aura_hora_valida(hora) && (mia == 0 || (mia > hora ? mia - hora : hora - mia) > 2)) {
        hora_ajustar_ms((uint64_t)hora * 1000);
    }
    cola_muestra_t cabeza;
    if (!cola_mirar(&cabeza) || memcmp(cabeza.ingest_id, t->payload, AURA_INGEST_ID_BYTES) != 0) {
        return;                                        /* confirmación atrasada de otra muestra */
    }
    if (cola_sacar()) {
        char id[37];
        aura_ingest_id_texto(cabeza.ingest_id, id);
        ESP_LOGI(TAG, "AURA confirmó %s, quedan %lu", id, (unsigned long)cola_cantidad());
        en_vuelo = false;
        intentos = 0;
        proximo_envio = xTaskGetTickCount();
    }
}

/* espera: cuánto esperar la primera (así un comando se atiende apenas llega) */
static void procesar_recibidas(TickType_t espera)
{
    recibida_t r;
    while (xQueueReceive(cola_rx, &r, espera) == pdTRUE) {
        espera = 0;
        aura_trama_t t;
        memcpy(&t, r.datos, r.largo);
        /* Lo que manda la raíz (o, en modo aula, el concentrador: su botón y la hora) es lo único
           que puede mandarle algo a una hoja. Cualquier otro nodo de la malla se ignora. */
        bool de_la_raiz = aura_es_la_raiz(r.de);
        bool de_mi_concentrador = modo == MODO_AULA && memcmp(r.de, concentrador.addr, 6) == 0;
        if (!de_la_raiz && !(de_mi_concentrador && (t.tipo == AURA_TIPO_COMANDO || t.tipo == AURA_TIPO_HORA))) {
            ESP_LOGW(TAG, "Trama %u de " MACSTR " (no es la raíz ni mi concentrador): se descarta", t.tipo,
                     MAC2STR(r.de));
            continue;
        }
        switch (t.tipo) {
        case AURA_TIPO_CONFIRMACION:
            procesar_confirmacion(&t);
            break;
        case AURA_TIPO_COMANDO:
            procesar_comando(&r, &t);
            break;
        case AURA_TIPO_HORA:
            if (t.largo == 8) {
                hora_ajustar_ms(aura_u64_leer(t.payload));
            }
            break;
        case AURA_TIPO_EVENTO_OK:
            if (t.largo == 2) {
                evento_confirmado((uint16_t)(t.payload[0] | t.payload[1] << 8));
            }
            aura_fw_confirmar();                       /* habla con la raíz: la versión anda */
            break;
        case AURA_TIPO_REPORTAR: {
            /* Volvió el broker: reporte del estado actual y lo pendiente ya, cada hoja en un
               momento distinto (0 a 3 s) para no amontonarse */
            ESP_LOGI(TAG, "La raíz pide reporte (volvió el broker)");
            nodo_mesh_reportar();
            TickType_t ya = xTaskGetTickCount() + pdMS_TO_TICKS(esp_random() % 3000);
            en_vuelo = false;
            intentos = 0;
            proximo_envio = ya;
            xSemaphoreTake(lock_eventos, portMAX_DELAY);
            evento_en_vuelo = false;
            xSemaphoreGive(lock_eventos);
            break;
        }
        default:
            break;
        }
    }
}

static void tarea_rx(void *arg)
{
    static uint8_t buf[MESH_MPS];
    while (true) {
        mesh_addr_t de;
        mesh_data_t d = {.data = buf, .size = sizeof(buf)};
        int flag = 0;
        if (esp_mesh_recv(&de, &d, portMAX_DELAY, &flag, NULL, 0) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (aura_es_ota(buf, d.size)) {                /* actualización: tiene su propia tarea */
            aura_ota_recibir(de.addr, buf, d.size);
            continue;
        }
        if (aura_trama_validar(buf, d.size) != AURA_TRAMA_OK) {
            descartadas_rx++;
            continue;
        }
        recibida_t r;
        memcpy(r.de, de.addr, 6);
        r.largo = d.size;
        memcpy(r.datos, buf, d.size);
        if (xQueueSend(cola_rx, &r, 0) != pdTRUE) {
            descartadas_rx++;
        }
    }
}

static void tarea_nodo(void *arg)
{
    TickType_t ultimo_pedido_hora = 0, ultimo_respaldo = 0;
    while (true) {
        procesar_recibidas(pdMS_TO_TICKS(50));
        /* Cada 5 s, haya conexión o no: fecha lo medido sin hora y respalda en flash lo que
           lleva COLA_A_FLASH_S sin confirmar */
        if (xTaskGetTickCount() - ultimo_respaldo >= pdMS_TO_TICKS(5000)) {
            ultimo_respaldo = xTaskGetTickCount();
            cola_respaldar(uptime_s(), ahora_epoca(), false);
        }
        if (!conectado) {
            continue;
        }
        if (reconecto) {
            reconecto = false;
            nodo_mesh_reportar();
        }
        TickType_t ahora = xTaskGetTickCount();
        if (ahora_epoca() == 0 && (ultimo_pedido_hora == 0 || ahora - ultimo_pedido_hora >= pdMS_TO_TICKS(PEDIR_HORA_MS))) {
            ultimo_pedido_hora = ahora;
            aura_trama_t t;
            aura_hora_armar(&t, siguiente_seq(), 0);   /* pedido: sin payload */
            enviar(&t);
        }
        enviar_estado();                               /* primero: la pantalla del aula al día */
        enviar_evento();
        procesar_recibidas(0);                         /* un comando no espera a la próxima vuelta */
        enviar_muestra();
    }
}

/* ================================================================== */
/*  Elección del padre                                                 */
/* ================================================================== */
/* Por cada beacon/probe response con vendor IE (tarea WiFi: debe ser breve) */
static void vendor_ie_cb(void *ctx, wifi_vendor_ie_type_t tipo, const uint8_t sa[6],
                         const vendor_ie_data_t *ie, int rssi)
{
    if (ie->vendor_oui[0] != AURA_OUI_0 || ie->vendor_oui[1] != AURA_OUI_1 ||
        ie->vendor_oui[2] != AURA_OUI_2 || ie->vendor_oui_type != AURA_IE_CONCENTRADOR ||
        ie->length < 4 + 6) {
        return;
    }
    taskENTER_CRITICAL(&lock_candidatos);
    size_t largo_aula = strlen(aula);
    bool mia = largo_aula > 0 && ie->length == 4 + 6 + largo_aula &&
               memcmp(ie->payload + 6, aula, largo_aula) == 0;
    int i;
    for (i = 0; i < cant_candidatos && memcmp(candidatos[i].bssid, sa, 6) != 0; i++) {
    }
    if (i < MAX_CANDIDATOS) {
        memcpy(candidatos[i].bssid, sa, 6);
        memcpy(candidatos[i].mac_malla, ie->payload, 6);
        candidatos[i].de_mi_aula = mia;
        if (i == cant_candidatos) {
            cant_candidatos++;
        }
    }
    taskEXIT_CRITICAL(&lock_candidatos);
}

/* 0 = no es concentrador, 1 = de otra aula, 2 = de mi aula */
static int tipo_concentrador(const uint8_t bssid[6], uint8_t mac_malla[6])
{
    int tipo = 0;
    taskENTER_CRITICAL(&lock_candidatos);
    for (int i = 0; i < cant_candidatos; i++) {
        if (memcmp(candidatos[i].bssid, bssid, 6) == 0) {
            memcpy(mac_malla, candidatos[i].mac_malla, 6);
            tipo = candidatos[i].de_mi_aula ? 2 : 1;
            break;
        }
    }
    taskEXIT_CRITICAL(&lock_candidatos);
    return tipo;
}

/* solo_canal_actual: escaneo rápido sin perder la conexión (chequeo en plan B) */
static void iniciar_escaneo(bool solo_canal_actual)
{
    wifi_scan_config_t sc = {
        .show_hidden = true,                           /* el AP de la malla es oculto */
        .scan_type = WIFI_SCAN_TYPE_PASSIVE,
        .scan_time.passive = 600,                      /* el beacon de la malla sale cada 300 ms */
    };
    escaneo_de_canal = false;
    if (solo_canal_actual) {
        wifi_second_chan_t sec;
        esp_wifi_get_channel(&sc.channel, &sec);
    } else if (probar_canal && canal_conocido) {
        probar_canal = false;
        escaneo_de_canal = true;
        sc.channel = canal_conocido;
        sc.scan_time.passive = ESCUCHA_CANAL_MS;
    }
    taskENTER_CRITICAL(&lock_candidatos);
    cant_candidatos = 0;
    taskEXIT_CRITICAL(&lock_candidatos);
    esp_wifi_scan_stop();
    if (esp_wifi_scan_start(&sc, false) == ESP_OK) {
        escaneando = true;
        if (escaneo_de_canal) {
            ESP_LOGI(TAG, "Busco a mi padre en el canal %u (el de la última vez)", canal_conocido);
        } else if (!solo_canal_actual) {
            if (aula[0]) {
                ESP_LOGI(TAG, "Buscando al concentrador de %s...", aula);
            } else {
                ESP_LOGI(TAG, "Sin aula: busco la raíz o un concentrador");
            }
        }
    }
}

static void conectar(const wifi_ap_record_t *rec, const mesh_assoc_t *assoc, const uint8_t *mac_malla,
                     modo_t nuevo_modo)
{
    wifi_config_t padre = {0};
    padre.sta.channel = rec->primary;
    memcpy(padre.sta.ssid, rec->ssid, sizeof(rec->ssid));
    padre.sta.bssid_set = 1;
    memcpy(padre.sta.bssid, rec->bssid, 6);
    esp_mesh_set_ap_authmode(rec->authmode);
    if (rec->authmode != WIFI_AUTH_OPEN) {
        strlcpy((char *)padre.sta.password, cfg.mesh_clave, sizeof(padre.sta.password));
    }
    modo_pedido = nuevo_modo;
    if (nuevo_modo == MODO_AULA) {
        memcpy(concentrador_pedido.addr, mac_malla, 6);
    }
    ESP_LOGI(TAG, "Padre elegido (%s): " MACSTR " capa %d, canal %d, RSSI %d",
             nuevo_modo == MODO_AULA ? "MODO AULA, mi concentrador" :
             aula[0] ? "PLAN B, envío directo" : "SIN AULA, envío directo",
             MAC2STR(rec->bssid), assoc->layer, rec->primary, rec->rssi);
    ultimo_intento = xTaskGetTickCount();
    /* IDF 6.1 rechaza MESH_LEAF en set_parent (ESP_FAIL al instante) y desconecta en bucle
       si se cambia a MESH_LEAF ya conectada (probado 2026-10-20). Se intenta MESH_LEAF una
       vez (por si otro IDF lo acepta) y si no, MESH_NODE: al conectarse se deja el AP casi
       mudo y se rechaza cualquier hijo (ver PARENT_CONNECTED). */
    static bool leaf_rechazado = false;
    esp_err_t err = ESP_FAIL;
    if (!leaf_rechazado) {
        err = esp_mesh_set_parent(&padre, (mesh_addr_t *)&assoc->mesh_id, MESH_LEAF, assoc->layer + 1);
        if (err != ESP_OK) {
            leaf_rechazado = true;
            ESP_LOGW(TAG, "Este IDF rechaza MESH_LEAF en set_parent: se usa MESH_NODE con el AP silenciado");
        }
    }
    if (err != ESP_OK) {
        err = esp_mesh_set_parent(&padre, (mesh_addr_t *)&assoc->mesh_id, MESH_NODE, assoc->layer + 1);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_mesh_set_parent: %s", esp_err_to_name(err));
    }
}

static void procesar_escaneo(int num)
{
    mesh_assoc_t assoc, mio_assoc, alt_assoc;
    wifi_ap_record_t rec, mio_rec, alt_rec;
    uint8_t mac_malla[6], mio_mac[6];
    bool hay_mio = false, hay_alt = false, padre_visto = false;
    int ie_len = 0, tipo_padre = 0;

    for (int i = 0; i < num; i++) {
        esp_mesh_scan_get_ap_ie_len(&ie_len);
        esp_mesh_scan_get_ap_record(&rec, &assoc);
        if (ie_len != sizeof(assoc)) {
            continue;                                  /* no es un nodo de la malla */
        }
        int tipo = tipo_concentrador(rec.bssid, mac_malla);
        if (conectado && memcmp(rec.bssid, bssid_padre, 6) == 0) {
            padre_visto = true;
            tipo_padre = tipo;
        }
        if (assoc.layer < 1 || assoc.layer >= MESH_MAX_CAPAS) {
            continue;
        }
        if (tipo == 2 && (!hay_mio || rec.rssi > mio_rec.rssi)) {
            mio_rec = rec;
            mio_assoc = assoc;
            memcpy(mio_mac, mac_malla, 6);
            hay_mio = true;
        }
        /* Plan B: la raíz u otro concentrador con lugar, nunca una hoja.
           Preferencia: menor capa y, a igual capa, mejor señal. */
        bool es_padre = conectado && memcmp(rec.bssid, bssid_padre, 6) == 0;
        if ((assoc.layer == 1 || tipo == 1) && !es_padre && assoc.assoc < assoc.assoc_cap &&
            (!hay_alt || assoc.layer < alt_assoc.layer ||
             (assoc.layer == alt_assoc.layer && rec.rssi > alt_rec.rssi))) {
            alt_rec = rec;
            alt_assoc = assoc;
            hay_alt = true;
        }
    }
    esp_mesh_flush_scan_result();
    escaneando = false;

    if (conectado) {
        static int ajeno = 0;                          /* dos revisiones seguidas, por un beacon perdido */
        ajeno = (modo == MODO_AULA && padre_visto && tipo_padre != 2) ? ajeno + 1 : 0;
        if (ajeno >= 2) {
            /* Mi concentrador ya no anuncia mi aula (se la cambiaron): a buscar de nuevo */
            ajeno = 0;
            /* Se cambia de padre sin desconectarse (como al volver del plan B): si se desconecta,
               la pila vuelve sola al mismo padre en 0,2 s. */
            if (hay_mio) {
                ESP_LOGW(TAG, "Mi concentrador ya no es de %s: me paso a otro de %s", aula, aula);
                conectar(&mio_rec, &mio_assoc, mio_mac, MODO_AULA);
            } else if (hay_alt) {
                ESP_LOGW(TAG, "Mi concentrador ya no es de %s: paso a plan B", aula);
                conectar(&alt_rec, &alt_assoc, NULL, MODO_DIRECTO);
            } else {
                ESP_LOGW(TAG, "Mi concentrador ya no es de %s y no veo otro padre: me desconecto", aula);
                esp_mesh_set_self_organized(false, false);
                esp_mesh_disconnect();
            }
            return;
        }
        if (ajeno == 1) {
            revisiones_rapidas = 1;                    /* confirmar en la próxima vuelta (3 s) */
        }
        if (modo == MODO_DIRECTO && hay_mio) {         /* chequeo en plan B: volvió mi concentrador */
            ESP_LOGI(TAG, "Apareció el concentrador de %s: dejo el envío directo", aula);
            busquedas_fallidas = 0;
            conectar(&mio_rec, &mio_assoc, mio_mac, MODO_AULA);
        }
        return;
    }
    if (hay_mio) {
        busquedas_fallidas = 0;
        conectar(&mio_rec, &mio_assoc, mio_mac, MODO_AULA);
        return;
    }
    if (escaneo_de_canal && aula[0]) {
        ESP_LOGI(TAG, "No está en el canal %u: busco en todos", canal_conocido);
        return;                                        /* no cuenta para el plan B */
    }
    busquedas_fallidas++;
    if (aula[0] && busquedas_fallidas < BUSQUEDAS_PLAN_B) {
        ESP_LOGW(TAG, "El concentrador de %s no está visible (%d/%d), se reintenta",
                 aula, busquedas_fallidas, BUSQUEDAS_PLAN_B);
    } else if (hay_alt) {
        conectar(&alt_rec, &alt_assoc, NULL, MODO_DIRECTO);
    } else {
        ESP_LOGW(TAG, "No veo ningún padre posible (¿gateway encendido? ¿mismo MESH_ID?), se reintenta");
    }
}

static void tarea_vigia(void *arg)
{
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        if (escaneando) {
            continue;
        }
        TickType_t ahora = xTaskGetTickCount();
        if (conectado) {
            /* Plan B: ¿volvió mi concentrador? Modo aula: ¿mi padre sigue siendo de mi aula? */
            if (aula[0] &&
                (revisiones_rapidas > 0 || (ahora - ultima_revision) >= pdMS_TO_TICKS(REVISAR_AULA_MS))) {
                if (revisiones_rapidas > 0) {
                    revisiones_rapidas--;
                }
                ultima_revision = ahora;
                iniciar_escaneo(true);
            }
            continue;
        }
        if (ultimo_intento != 0 && (ahora - ultimo_intento) < pdMS_TO_TICKS(ESPERA_CONEXION_MS)) {
            continue;                                  /* se está intentando conectar */
        }
        /* Con la auto-organización apagada la pila reintenta sin fin el último padre y
           mientras tanto no deja escanear: tras un margen se cortan los reintentos. */
        if (reintentando && (ahora - desconectada_desde) < pdMS_TO_TICKS(ESPERA_RECONEXION_MS)) {
            continue;
        }
        if (reintentando || ultimo_intento != 0) {
            ESP_LOGW(TAG, "El padre no responde: dejo de reintentar y busco otro");
            esp_mesh_set_self_organized(false, false);
            reintentando = false;
            ultimo_intento = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        iniciar_escaneo(false);
    }
}

/* El AP de la hoja casi no emite beacons: nadie tiene que colgarse de una hoja */
static void silenciar_ap(void)
{
    wifi_config_t ap;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap) == ESP_OK && ap.ap.beacon_interval != BEACON_HOJA_TU) {
        ap.ap.beacon_interval = BEACON_HOJA_TU;
        if (esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) {
            ESP_LOGW(TAG, "No se pudo bajar el beacon del AP");
        }
    }
}

static void manejador_mesh(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case MESH_EVENT_STARTED:
        esp_mesh_set_self_organized(false, false);    /* el padre lo elige procesar_escaneo() */
        iniciar_escaneo(false);
        break;
    case MESH_EVENT_SCAN_DONE:
        procesar_escaneo(((mesh_event_scan_done_t *)data)->number);
        break;
    case MESH_EVENT_PARENT_CONNECTED: {
        mesh_event_connected_t *c = (mesh_event_connected_t *)data;
        if (modo_pedido == MODO_AULA) {
            memcpy(concentrador.addr, concentrador_pedido.addr, 6);
        }
        modo = modo_pedido;
        memcpy(bssid_padre, c->connected.bssid, 6);
        canal_guardar(c->connected.channel);
        estado_pendiente = true;                       /* el concentrador queda sincronizado ya */
        silenciar_ap();
        conectado = true;
        reconecto = true;
        fallos_envio = 0;
        ultima_revision = xTaskGetTickCount();
        ESP_LOGI(TAG, "Conectada a " MACSTR " - capa %d - %s", MAC2STR(c->connected.bssid),
                 esp_mesh_get_layer(), modo == MODO_AULA ? "MODO AULA (todo pasa por el concentrador)" :
                 "envío directo a la raíz");
        break;
    }
    case MESH_EVENT_PARENT_DISCONNECTED:
        if (conectado) {
            ESP_LOGW(TAG, "Desconectada del padre");
            desconectada_desde = xTaskGetTickCount();
            reintentando = true;
            probar_canal = true;                       /* lo más probable: vuelve en el mismo canal */
            if (modo == MODO_AULA) {
                revisiones_rapidas = 2;                /* si la pila la reconecta sola, ver si el padre sigue siendo de mi aula */
            }
        }
        conectado = false;
        break;
    case MESH_EVENT_CHILD_CONNECTED: {
        mesh_event_child_connected_t *c = (mesh_event_child_connected_t *)data;
        ESP_LOGW(TAG, "Un nodo (" MACSTR ") quiso colgarse de esta hoja: se lo rechaza", MAC2STR(c->mac));
        esp_wifi_deauth_sta(c->aid);
        break;
    }
    default:
        break;
    }
}

/* ================================================================== */
/*  Inicio                                                             */
/* ================================================================== */
/* Antes de reiniciarse con una versión nueva: que lo pendiente no se pierda. Los eventos (que
   viven en RAM) salen primero; las mediciones sin confirmar pasan a la flash y se recuperan al
   arrancar. */
static void antes_de_reiniciar(void)
{
    /* Los reportes pendientes se descartan: dirían la versión vieja, y la raíz tomaría eso como
       una vuelta atrás. Al arrancar se manda uno nuevo. */
    xSemaphoreTake(lock_eventos, portMAX_DELAY);
    int quedan = 0;
    for (int i = 0; i < eventos_cant; i++) {
        aura_trama_t *e = &eventos[(eventos_cabeza + i) % EVENTOS_CAP];
        if (e->tipo == AURA_TIPO_REPORTE) {
            if (i == 0) {
                evento_en_vuelo = false;
            }
            continue;
        }
        eventos[(eventos_cabeza + quedan) % EVENTOS_CAP] = *e;
        quedan++;
    }
    eventos_cant = quedan;
    xSemaphoreGive(lock_eventos);
    TickType_t desde = xTaskGetTickCount();
    while (xTaskGetTickCount() - desde < pdMS_TO_TICKS(ESPERA_EVENTOS_MS)) {
        xSemaphoreTake(lock_eventos, portMAX_DELAY);
        int pendientes = eventos_cant;
        xSemaphoreGive(lock_eventos);
        if (pendientes == 0) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (eventos_cant) {
        ESP_LOGW(TAG, "Me reinicio con %d eventos sin confirmar", eventos_cant);
    }
    cola_respaldar(uptime_s(), ahora_epoca(), true);
}

esp_err_t nodo_mesh_iniciar(const nodo_mesh_config_t *c)
{
    cfg = *c;
    aura_fw_iniciar();
    /* seq arranca al azar: si arrancara siempre en 0, el primer evento tras un reinicio podría
       llevar la misma seq que el último de antes y el que lo recibe lo tomaría por repetido
       (pasaba con el reporte tras reiniciarse: la raíz lo confirmaba sin publicarlo) */
    seq = (uint16_t)esp_random();
    esp_read_mac(mi_mac, ESP_MAC_WIFI_STA);
    nvs_cargar();
    char hw[AURA_HW_ID_BYTES];
    aura_hw_id_de_mac(mi_mac, hw);
    ESP_LOGI(TAG, "Hoja %s, aula %s", hw, aula[0] ? aula : "(sin asignar: la asigna AURA)");

    if (cola_abrir() != ESP_OK) {
        ESP_LOGE(TAG, "Sin cola: las mediciones no se van a guardar");
    }
    lock_eventos = xSemaphoreCreateMutex();
    cola_rx = xQueueCreate(6, sizeof(recibida_t));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta = NULL, *ap = NULL;
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&sta, &ap));
    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_country_code("AR", false));   /* canales 1 a 13 */
    ESP_ERROR_CHECK(esp_wifi_set_vendor_ie_cb(vendor_ie_cb, NULL));
    ESP_ERROR_CHECK(esp_wifi_start());
    if (cfg.potencia_tx > 0) {
        esp_wifi_set_max_tx_power(cfg.potencia_tx);
    }
    int8_t potencia;
    esp_wifi_get_max_tx_power(&potencia);
    ESP_LOGI(TAG, "Potencia de transmisión: %.2f dBm", potencia / 4.0);

    ESP_ERROR_CHECK(esp_mesh_init());
    ESP_ERROR_CHECK(esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &manejador_mesh, NULL));
    ESP_ERROR_CHECK(esp_mesh_fix_root(true));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(MESH_MAX_CAPAS));
    ESP_ERROR_CHECK(esp_mesh_disable_ps());
    ESP_ERROR_CHECK(esp_mesh_set_ap_authmode(WIFI_AUTH_WPA2_PSK));

    mesh_cfg_t mc = MESH_INIT_CONFIG_DEFAULT();
    memcpy((uint8_t *)&mc.mesh_id, cfg.mesh_id, 6);
    mc.channel = 0;                                    /* el canal sale del escaneo */
    mc.allow_channel_switch = true;
    mc.router.ssid_len = strlen(ROUTER_FICTICIO);
    memcpy(mc.router.ssid, ROUTER_FICTICIO, mc.router.ssid_len);
    mc.mesh_ap.max_connection = 1;                     /* el mínimo que admite la API */
    mc.mesh_ap.nonmesh_max_connection = 0;
    strlcpy((char *)mc.mesh_ap.password, cfg.mesh_clave, sizeof(mc.mesh_ap.password));
    ESP_ERROR_CHECK(esp_mesh_set_config(&mc));
    ESP_ERROR_CHECK(esp_mesh_send_block_time(LIMITE_ENVIO_MS));
    /* Antes de arrancar la malla: registra el evento que informa la raíz, y sin ella se descarta todo el control */
    aura_ota_receptor_cfg_t ota = {.antes_de_reiniciar = antes_de_reiniciar};
    aura_ota_receptor_iniciar(&ota);
    ESP_ERROR_CHECK(esp_mesh_start());

    xTaskCreate(tarea_rx, "mesh_rx", 4096, NULL, 5, NULL);
    xTaskCreate(tarea_nodo, "nodo", 6144, NULL, 5, NULL);
    xTaskCreate(tarea_vigia, "vigia", 3072, NULL, 4, NULL);
    aura_fw_antes_de_volver(antes_de_reiniciar);   /* lo medido con la versión nueva no se pierde */
    return ESP_OK;
}
