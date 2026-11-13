#include "configuracion.h"
#include "energia.h"
#include "hardware.h"
#include "logica.h"
#include "bus_i2c.h"
#include "pantalla.h"
#include "reloj.h"
#include "sondas.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nodo_mesh.h"
#include "nvs_flash.h"
#include <math.h>
#include <string.h>

#if __has_include("config_local.h")
#include "config_local.h"
#else
#error "Copiar main/config_local.ejemplo.h como main/config_local.h y completar los datos de la mesh"
#endif

static const char *CAMPOS[2] = {"temp_heladera_c", "temp_freezer_c"};
static bool mesh_iniciada;

static void tarea_dispositivo(void *arg)
{
    (void)arg;
    float temperaturas[2] = {NAN, NAN};
    uint8_t validas = 0;
    int64_t ultima[2] = {0, 0};
    int64_t ultimo_display = 0;
    hfr_alarma_t alarmas[2] = {0};
    hfr_config_t anterior = hfr_config_actual();
    int64_t desconexion_en = esp_timer_get_time();
    while (true) {
        int64_t ahora = esp_timer_get_time();
        hfr_config_t cfg = hfr_config_actual();
        hfr_energia_actualizar(mesh_iniciada);
        uint32_t intervalos[2] = {cfg.intervalo_s, cfg.freezer_s};
        cJSON *medicion = cJSON_CreateObject();
        bool hubo_lectura = false;
        for (unsigned i = 0; i < 2; ++i) {
            if (cfg.minimo[i] != anterior.minimo[i] || cfg.maximo[i] != anterior.maximo[i]) {
                alarmas[i] = (hfr_alarma_t){0};
            }
            if (ultima[i] != 0 && ahora - ultima[i] < (int64_t)intervalos[i] * 1000000) {
                continue;
            }
            hubo_lectura = true;
            ultima[i] = ahora;
            float temperatura = NAN;
            bool leida = hfr_sonda_leer(i, &temperatura);
            hfr_sonda_estado_t estado = hfr_clasificar(temperatura, leida);
            temperaturas[i] = temperatura;
            validas &= ~(1U << i);
            if (estado == HFR_SONDA_OK) {
                validas |= 1U << i;
                hfr_alarma_actualizar(&alarmas[i], temperatura, cfg.minimo[i], cfg.maximo[i]);
                if (medicion != NULL) {
                    cJSON_AddNumberToObject(medicion, CAMPOS[i], round((double)temperatura * 100) / 100);
                }
                ESP_LOGI("SONDA", "%s = %.2f C", CAMPOS[i], (double)temperatura);
            }
            if (mesh_iniciada) {
                nodo_mesh_sonda(CAMPOS[i], estado == HFR_SONDA_OK,
                                estado == HFR_SIN_RESPUESTA ? "sin_respuesta" : "fuera_de_rango");
            }
        }
        anterior = cfg;
        if (hubo_lectura && medicion != NULL && cJSON_GetArraySize(medicion) > 0) {
            if (!mesh_iniciada || !nodo_mesh_medicion(medicion)) {
                ESP_LOGE("MEDICION", "Lectura no encolada; revisar inicializacion o memoria");
            }
        }
        cJSON_Delete(medicion);

        bool conectada = mesh_iniciada && nodo_mesh_conectada();
        if (conectada) {
            desconexion_en = ahora;
        }
        hfr_indicadores(alarmas[0].activa != 0 || alarmas[1].activa != 0, conectada);
        if (ahora - ultimo_display >= 1000000) {
            ultimo_display = ahora;
            bool oscura = hfr_energia_bateria() ||
                          ahora - desconexion_en >= (int64_t)HFR_OSCURO_TRAS_MS * 1000;
            uint32_t pendientes = mesh_iniciada ? nodo_mesh_pendientes() : 0;
            hfr_pantalla_actualizar(temperaturas, validas, conectada, pendientes,
                                   hfr_energia_bateria(), hfr_reloj_utc(), oscura);
            hfr_reloj_mantener(conectada);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("NVS", "NVS incompatible o sin espacio: se restauran los ajustes iniciales");
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    bool nvs_lista = err == ESP_OK;
    if (!nvs_lista) {
        ESP_LOGE("NVS", "No disponible: %s. Solo diagnostico local", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(hfr_config_iniciar(nvs_lista));
    hfr_i2c_iniciar();
    hfr_reloj_iniciar();
    hfr_pantalla_iniciar();
    hfr_sondas_iniciar();
    hfr_energia_iniciar(nvs_lista);

    nodo_mesh_config_t cfg = {
        .mesh_id = {MESH_ID_BYTES},
        .mesh_clave = MESH_AP_PASS,
        .potencia_tx = HFR_POTENCIA_TX,
        .alimentacion = "desconocida",
        .aplicar_config = hfr_config_aplicar,
        .describir_config = hfr_config_describir,
    };
    const uint8_t vacio[6] = {0};
    bool credenciales = memcmp(cfg.mesh_id, vacio, sizeof(vacio)) != 0 &&
                        strlen(cfg.mesh_clave) >= 8 && strlen(cfg.mesh_clave) <= 63 &&
                        strcmp(cfg.mesh_clave, "CAMBIAR") != 0;
    if (nvs_lista && credenciales) {
        err = nodo_mesh_iniciar(&cfg);
        mesh_iniciada = err == ESP_OK;
        if (!mesh_iniciada) {
            ESP_LOGE("MESH", "No se pudo iniciar: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGE("MESH", "Sin datos de mesh validos o sin NVS: solo diagnostico local");
    }
    if (xTaskCreate(tarea_dispositivo, "heladera", 6144, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE("HFR01", "No hay memoria para iniciar las mediciones");
    }
}
