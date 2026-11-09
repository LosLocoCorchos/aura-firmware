#include "mq2.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mq2_calc.h"

#define MQ2_GPIO        3
#define MUESTRAS        10
#define ATENUACION      ADC_ATTEN_DB_12      /* ESP32-C3: rango útil ~0 a 2,5 V en el pin (ver mq2_calc.h) */
#define CRUDO_SATURADO  4090                 /* 12 bits: pegado al techo del ADC, con o sin calibración */

static const char *TAG = "MQ2";
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t cali;               /* NULL = sin calibración de fábrica */
static adc_channel_t canal;

esp_err_t mq2_iniciar(void)
{
    adc_unit_t unidad;
    esp_err_t e = adc_oneshot_io_to_channel(MQ2_GPIO, &unidad, &canal);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d no es un pin de ADC: %s", MQ2_GPIO, esp_err_to_name(e));
        return e;
    }
    adc_oneshot_unit_init_cfg_t u = {.unit_id = unidad};
    e = adc_oneshot_new_unit(&u, &adc);
    if (e != ESP_OK) {
        return e;
    }
    adc_oneshot_chan_cfg_t c = {.atten = ATENUACION, .bitwidth = ADC_BITWIDTH_DEFAULT};
    e = adc_oneshot_config_channel(adc, canal, &c);
    if (e != ESP_OK) {
        return e;
    }
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cc = {
        .unit_id = unidad,
        .chan = canal,
        .atten = ATENUACION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cc, &cali) != ESP_OK) {
        cali = NULL;
    }
#endif
    ESP_LOGI(TAG, "ADC listo en GPIO%d, %s", MQ2_GPIO, cali ? "calibrado de fábrica" : "SIN calibración (aproximado)");
    return ESP_OK;
}

static bool leer_pin_mv(uint32_t *mv, int *crudo_out)
{
    int crudo;
    if (adc == NULL || adc_oneshot_read(adc, canal, &crudo) != ESP_OK) {
        return false;
    }
    *crudo_out = crudo;
    int v;
    if (cali != NULL) {
        if (adc_cali_raw_to_voltage(cali, crudo, &v) != ESP_OK) {
            return false;
        }
    } else {
        v = crudo * (int)MQ2_FONDO_ESCALA_MV / 4095;  /* aproximación lineal, escala de la C3 a 12 dB */
    }
    *mv = v < 0 ? 0 : (uint32_t)v;
    return true;
}

mq2_estado_t mq2_leer(uint32_t *ao_mv)
{
    uint32_t suma = 0, minimo = UINT32_MAX, maximo = 0, suma_crudo = 0;
    int validas = 0;
    for (int i = 0; i < MUESTRAS; i++) {
        uint32_t mv;
        int crudo;
        if (leer_pin_mv(&mv, &crudo)) {
            suma += mv;
            suma_crudo += (uint32_t)crudo;
            validas++;
            if (mv < minimo) {
                minimo = mv;
            }
            if (mv > maximo) {
                maximo = mv;
            }
        }
        vTaskDelay(1);                                 /* una muestra por tick */
    }
    if (validas == 0) {
        ESP_LOGW(TAG, "el ADC no devolvió ninguna lectura");
        return MQ2_SIN_RESPUESTA;
    }
    uint32_t pin_mv = suma / (uint32_t)validas;
    uint32_t crudo_prom = suma_crudo / (uint32_t)validas;
    ESP_LOGD(TAG, "pin: min=%lu max=%lu prom=%lu mV (ruido %lu), crudo %lu", (unsigned long)minimo,
             (unsigned long)maximo, (unsigned long)pin_mv, (unsigned long)(maximo - minimo), (unsigned long)crudo_prom);
    if (crudo_prom >= CRUDO_SATURADO) {
        pin_mv = MQ2_PIN_MAX_MV;                       /* el ADC está en el techo, diga lo que diga la calibración */
    }
    mq2_estado_t e = mq2_pin_a_ao_mv(pin_mv, ao_mv);
    if (e == MQ2_SATURADA) {
        ESP_LOGW(TAG, "ADC saturado (%lu mV en el pin): el gas es al menos %u mV en el AO", (unsigned long)pin_mv,
                 (unsigned)MQ2_AO_SATURADO_MV);
    } else if (e == MQ2_PEGADA_AL_PISO) {
        ESP_LOGW(TAG, "lectura en 0 mV: revisar divisor, cableado del AO y alimentación del MQ2");
    }
    return e;
}
