#include "lector.h"
#include "pines.h"
#include "esp_log.h"
#include "rc522.h"
#include "driver/rc522_spi.h"
#include "rc522_picc.h"

static const char *TAG = "LECTOR";

/* Estas cuatro funciones del driver no estan en su API publica pero si exportadas (las usa
   rc522_start). Se declaran aca, a mano, para poder revisar el lector sin copiar el driver.
   La version de abobija/rc522 esta FIJA (4.0.1); si se cambia hay que revisar esto.
   Los registros: VersionReg 0x37 (0x91 o 0x92 en un MFRC522 vivo) y TxControlReg 0x14
   (bits 0 y 1 = antena). */
extern esp_err_t rc522_pcd_reset(const rc522_handle_t rc522, uint32_t timeout_ms);
extern esp_err_t rc522_pcd_init(const rc522_handle_t rc522);
extern esp_err_t rc522_pcd_rw_test(const rc522_handle_t rc522);
extern esp_err_t rc522_pcd_read(const rc522_handle_t rc522, int addr, uint8_t *valor);
#define REG_TX_CONTROL 0x14
#define REG_VERSION 0x37

static rc522_handle_t lector;           /* NULL hasta que el driver este creado y con su evento */
static bool arrancado;                  /* rc522_start() se completo: recien ahi la tarea lee tarjetas */
static QueueHandle_t cola_lecturas;
static SemaphoreHandle_t mutex_lector;

/* Corre en la tarea del lector: solo copia el UID a la cola y vuelve */
static void al_cambiar_tarjeta(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    (void)arg;
    (void)base;
    (void)id;
    const rc522_picc_state_changed_event_t *ev = (const rc522_picc_state_changed_event_t *)datos;
    if (ev->picc->state != RC522_PICC_STATE_ACTIVE) return;     /* una vez por cada vez que se apoya */
    lectura_t l = {.largo = ev->picc->uid.length};
    if (l.largo > UID_MAX_BYTES) return;
    memcpy(l.uid, ev->picc->uid.value, l.largo);
    xQueueSend(cola_lecturas, &l, 0);
}

esp_err_t lector_iniciar(QueueHandle_t cola)
{
    cola_lecturas = cola;
    mutex_lector = xSemaphoreCreateMutex();
    static spi_bus_config_t bus = {
        .miso_io_num = PIN_RC522_MISO,
        .mosi_io_num = PIN_RC522_MOSI,
        .sclk_io_num = PIN_RC522_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    static rc522_spi_config_t spi = {
        .host_id = SPI2_HOST,
        .bus_config = &bus,
        .dev_config = {.spics_io_num = PIN_RC522_SS},
        .rst_io_num = PIN_RC522_RST,
    };
    rc522_driver_handle_t driver;
    esp_err_t e = rc522_spi_create(&spi, &driver);
    if (e != ESP_OK) return e;
    e = rc522_driver_install(driver);
    if (e != ESP_OK) return e;
    rc522_config_t cfg = {.driver = driver, .task_mutex = mutex_lector};
    rc522_handle_t creado = NULL;
    e = rc522_create(&cfg, &creado);
    if (e != ESP_OK) return e;
    e = rc522_register_events(creado, RC522_EVENT_PICC_STATE_CHANGED, al_cambiar_tarjeta, NULL);
    if (e != ESP_OK) return e;              /* sin el evento no hay lectura posible: queda sin lector */
    lector = creado;
    /* start() hace el reinicio, la prueba de lectura/escritura y la configuracion: si el RC522
       no esta conectado falla aca. La tarea del driver ya existe pero no lee hasta que start()
       termina bien, asi que lector_revisar() lo reintenta. */
    e = rc522_start(lector);
    arrancado = (e == ESP_OK);
    return e;
}

lector_salud_t lector_revisar(void)
{
    bool creado = lector != NULL;
    if (creado && xSemaphoreTake(mutex_lector, pdMS_TO_TICKS(500)) != pdTRUE) {
        return arrancado ? LECTOR_OK : LECTOR_CAIDO;       /* ocupado leyendo */
    }
    uint8_t version = 0, antena = 0;
    bool registros_bien = false;
    if (creado && arrancado) {
        esp_err_t ev = rc522_pcd_read(lector, REG_VERSION, &version);
        esp_err_t ea = rc522_pcd_read(lector, REG_TX_CONTROL, &antena);
        registros_bien = ev == ESP_OK && version != 0x00 && version != 0xFF && ea == ESP_OK && (antena & 0x03) == 0x03;
    }
    lector_paso_t paso = lector_proximo_paso(creado, arrancado, registros_bien);
    bool salio_bien = false;
    if (paso == LECTOR_PASO_ARRANCAR) {
        ESP_LOGW(TAG, "El RC522 nunca termino de arrancar: reintento rc522_start()");
        salio_bien = rc522_start(lector) == ESP_OK;
        arrancado = salio_bien;
    } else if (paso == LECTOR_PASO_REINICIAR) {
        ESP_LOGW(TAG, "El RC522 se reinicio o no responde (version=0x%02X antena=0x%02X): lo reinicio", version, antena);
        esp_err_t e = rc522_pcd_reset(lector, 150);
        if (e == ESP_OK) e = rc522_pcd_rw_test(lector);
        if (e == ESP_OK) e = rc522_pcd_init(lector);
        salio_bien = (e == ESP_OK);
    }
    if (creado) xSemaphoreGive(mutex_lector);
    return lector_salud_tras(paso, salio_bien);
}
