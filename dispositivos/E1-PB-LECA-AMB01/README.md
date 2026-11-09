# E1-PB-LECA-AMB01 — nodo de gas y CO2 del LabECA

Mide CO2, temperatura y humedad (SCD40) y un indicador de humo y vapores (MQ2) en el aire del laboratorio.
Es un **aviso de ventilación**, no un detector de gas ni un sistema de seguridad: en el LabECA no hay gas
combustible, hay soldadores de estaño (humo de flux), alcohol isopropílico de la limpieza y, como única
ventilación, las ventanas. El buzzer significa "abran las ventanas", no "evacuar". Un LED RGB indica el estado de
la conexión a la mesh y el del aviso. El aviso lo decide el nodo y funciona igual sin mesh y sin AURA.

- **CO2 alto:** hay mucha gente y no se renueva el aire.
- **Humo/vapores alto (MQ2):** el ambiente se cargó de humo o vapores. El MQ2 no dice qué es ni cuánto (no mide
  ppm y no está calibrado): sirve como indicador grueso. Medir bien el humo de soldadura pide un sensor de
  partículas (PM2.5) o de compuestos orgánicos (VOC); queda para más adelante.

| | |
|---|---|
| **Ubicación** | Edificio 1, planta baja, LabECA (posición exacta a definir al instalar) |
| **Responsable** | @LosLocoCorchos |
| **Estado** | en desarrollo |
| **Instalado** | todavía no |
| **Transporte** | mesh ESP-WIFI-MESH (interior), en ESP-IDF 6.1, según [ADR-007](../../docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md) |
| **Contrato que implementa** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.4 (propuesta) |
| **Tipo y versión del firmware** | `aura-gas-co2` 1.0.0 (regla en [`GUIA_NODOS_HOJA.md`](../../Mesh_Red_Completa/GUIA_NODOS_HOJA.md), sección 2.7) |
| **Basado en** | [`Mesh_Red_Completa/Mesh3_Hoja`](../../Mesh_Red_Completa/Mesh3_Hoja/) |
| **Códigos anteriores** | ninguno (antes de entrar a este repo, el proyecto se llamaba `Node_Gas_CO2_AURA` y estaba escrito en Arduino) |

## Qué hace

Es una **hoja** de la mesh sobre la biblioteca [`nodo_mesh`](../../Mesh_Red_Completa/componentes/nodo_mesh/):
no reenvía tráfico de otros. Al encender, los dos sensores se **calientan 2 minutos** (ver más abajo).
El aviso de CO2 funciona desde el arranque; el de humo empieza al terminar el calentamiento.
Después, cada 5 s mide y evalúa el aviso local; cada
`intervalo_s` publica la última medición válida de cada sonda. La conexión, el aula, la cola de
mediciones, la confirmación de AURA y la hora las resuelve la biblioteca. El nodo no conoce AURA: se
identifica por la MAC de su placa (`hw_id`), y todas las placas de este dispositivo llevan el mismo
binario.

## Hardware

| Componente | Modelo | Conexión |
|---|---|---|
| Placa | ESP32-C3 SuperMini (la pone el proyecto) | — |
| CO2, temperatura y humedad | Sensirion SCD40 (I2C, 0x62) | SDA GPIO4, SCL GPIO5, 3V3 |
| Estado de la conexión | Módulo LED RGB con resistencias (cátodo común) | R GPIO0, G GPIO1, B GPIO6, GND |
| Gas combustible | MQ2 (módulo Keyes), salida analógica AO | VCC a 5 V; AO → 10 kΩ → GPIO3 → 15 kΩ → GND (divisor 5 V → 3 V) |
| Buzzer | buzzer | GPIO10 → 1 kΩ → base de un NPN; buzzer entre 5 V y el colector |

El bus I2C (solo el SCD40) corre a 50 kHz, como en la versión anterior de este nodo. El LED se maneja
por PWM (LEDC) y no usa componentes externos salvo `espressif/cjson`, que ya trae la plantilla. Los
pines del LED no son de arranque (GPIO2/8/9) ni del puerto serie (GPIO20/21). Si el módulo es de ánodo
común, definir `LED_ANODO_COMUN 1` en [`main/led.c`](main/led.c).

## LED de estado

| Color | Significa |
|---|---|
| Verde fijo | conectado a la mesh |
| Amarillo parpadeando | intentando conectar: al arrancar, o sin conexión hace menos de 60 s |
| Rojo fijo | sin conexión hace 60 s o más |
| Azul que "respira" | conectado a la mesh y calentando sensores (2 min) |
| Magenta parpadeando rápido | aviso de CO2 o de humo/vapores activo o silenciado (prioridad sobre todo) |
| Cian, dos destellos cada 2 s | un sensor no da medición válida; entre destellos se ve el color de la conexión |

Prioridad: alarma > calentando (solo con conexión) > falla de sensor > conexión.

**El azul del calentamiento no tapa un problema de conexión:** durante los 2 minutos de calentamiento, si el nodo
todavía no se conectó se sigue viendo el amarillo parpadeando (o el rojo); cuando engancha a la mesh pasa a azul
y, al terminar el calentamiento, a verde. Así, al instalar, lo primero que se ve es si engancha.

La biblioteca solo informa si está conectada o no, así que "intentando" y "sin conexión" se distinguen
por el tiempo (`LED_REINTENTO_MS`, en [`main/led_patron.h`](main/led_patron.h), junto con los brillos).
Los tres últimos se pueden apagar con `LED_ESTADOS_EXTRA 0` en [`main/main.c`](main/main.c): queda solo el
estado de la conexión.

La **C3 SuperMini tiene la antena de chip mal adaptada**: `potencia_tx` está en 60 (15 dBm), pensado
para la antena de alambre de 31 mm de la
[guía](../../Mesh_Red_Completa/GUIA_NODOS_HOJA.md#6-alcance). No subir a 19,5 dBm con esa antena.

## Lo que envía a AURA

Cada medición es un campo de `values` (contrato §3.1). Una sonda que falla no aparece: se avisa con
una alerta `sensor`. Los rangos son los de [`conectores/E1-PB-LECA-AMB01/conector.toml`](../../conectores/E1-PB-LECA-AMB01/conector.toml):
lo que quede fuera, AURA no lo guarda.

| Campo | Unidad | Rango válido | Origen | Cada cuánto |
|---|---|---|---|---|
| `co2_ppm` | ppm | 0 a 40000 | SCD40 | `intervalo_s` (60 s por defecto) |
| `temp_c` | °C | -10 a 60 | SCD40 | `intervalo_s` |
| `hum_pct` | % HR | 0 a 100 | SCD40 | `intervalo_s` |
| `gas_mv` | mV | 0 a 4100 | MQ2 | `intervalo_s` |

`gas_mv` es la **tensión en el AO del MQ2**, ya compensada por el divisor: **no es ppm**. Pasarlo a
ppm requiere calibrar Ro del sensor en aire limpio y usar la curva del fabricante.

**Límite del ADC (ESP32-C3):** a 12 dB el rango útil del ADC es ~0 a 2500 mV (no los ~3100 mV de la ESP32 /
ESP32-S3). Con el divisor 10k/15k el pin llega a ese tope con el AO en ~4,17 V, así que **un AO de 4,1 a 5 V no se
puede medir**: el firmware lo detecta (`MQ2_SATURADA`, límite `MQ2_PIN_MAX_MV` = 2450 mV en el pin, o lectura
cruda ≥ 4090). Un MQ2 saturado es una **lectura válida y alta**, de "al menos" 4083 mV de AO (`MQ2_AO_SATURADO_MV`): se
publica `gas_mv` = 4083, la alerta lleva `saturado: true` y **no** cuenta como falla de la sonda (no sale
`alerts/sensor`). El umbral de aviso, 2400 mV de AO
(1440 mV en el pin), está muy por debajo del tope. Se usa la calibración de fábrica del ADC (`adc_cali`, curve
fitting); si no estuviera, se aproxima con 2500 mV de escala completa y el log lo avisa. Si hiciera falta medir
hasta 5 V de AO, bastaría un divisor 10k/10k (AO 5 V → 2,5 V en el pin): hay que cambiar también `MQ2_DIV_R2_OHM`.

Qué se publica y cuándo: cada `intervalo_s`, **la última medición válida y vigente de cada sonda** (no la
última lectura: una lectura mala suelta no hace desaparecer el campo). Una medición deja de ser vigente a los
30 s (`SONDA_VIGENCIA_MS`) o si la sonda está en falla declarada: en ese caso el campo no aparece. Si cambia una
causa cambia de estado se publica una muestra enseguida, sin esperar el intervalo.

Alertas que emite el nodo (contrato §3.5):

| Tipo | Cuándo | Severidad |
|---|---|---|
| `sensor` | una sonda **lleva 3 lecturas malas seguidas** (15 s) o el ADC del MQ2 lee 0 mV; una vez por cambio | `high`; al volver (2 lecturas buenas seguidas), `info` con `motivo: "recuperada"` |
| `dioxido_carbono_alto` | **cada cambio de estado** del aviso de CO2 (ver abajo) | `warning` al activarse; `info` al silenciarse y al normalizarse |
| `humo_vapores` | **cada cambio de estado** del aviso de humo/vapores | `warning` al activarse; `info` al silenciarse y al normalizarse |

`details` de las dos de aviso: `{"estado": "activa" | "silenciada" | "normalizada", "valor": ..., "umbral": ..., "saturado": ...}`.
Las emite el **firmware** en el instante del cambio (evalúa cada 5 s), y no el conector de AURA: el nodo es
quien actúa y informa qué hizo ("prendí el aviso", "me silenciaron", "se normalizó"), y el nodo publica una muestra
cada `intervalo_s`, que puede ser de minutos u horas. Los umbrales viven solo en `alarma.h`; el conector
([`conectores/E1-PB-LECA-AMB01/`](../../conectores/E1-PB-LECA-AMB01/)) solo guarda las mediciones. La alerta pasa
por la cola de `nodo_mesh`: si la mesh está caída, espera.

Si el SCD40 lleva 3 ciclos seguidos (15 s) sin dar una lectura válida, el firmware lo **reinicia**
(`scd4x_iniciar`), y vuelve a intentarlo cada 6 ciclos (30 s) mientras no conteste, también si no respondió en
el arranque. Una lectura de 0 ppm (el SCD40 no mide 0) se cuenta como falla.

## Aviso de ventilación (alarma local)

Lo decide el nodo con lo que mide, sin esperar a AURA. CO2 y humo/vapores son **causas independientes**, cada una
con tres estados (lógica en [`main/alarma.c`](main/alarma.c), probada en la PC):

| Estado | Buzzer | LED | Se entra cuando |
|---|---|---|---|
| normal | — | según la conexión | al arrancar, o desde activa/silenciada con lecturas válidas **bajo el valor normal durante 1 min** |
| activa | un pitido corto de 200 ms cada 10 s, no sirena | magenta | lecturas válidas **sobre el umbral durante 1 min** |
| silenciada | callado | magenta | `silence` con la causa activa |

| Causa | Se activa con | Se normaliza con |
|---|---|---|
| CO2 | `co2_ppm >= 1500` (`CO2_ALARMA_PPM`) | `< 1400` (`CO2_NORMAL_PPM`) |
| Humo/vapores | `gas_mv >= 2400` (`HUMO_ALARMA_MV`), o ADC saturado | `< 2200` (`HUMO_NORMAL_MV`) |

Los umbrales son constantes del firmware ([`main/alarma.h`](main/alarma.h)), el único lugar donde están. El de humo
es **fijo y está sin calibrar**: hay que ajustarlo una vez en el laboratorio, con aire limpio y con alguien
soldando (la base del MQ2 se corre con la temperatura, la humedad y el uso).

Reglas:

- **El minuto va en las dos direcciones** (`ALARMA_FILTRO_MS`): no suena con cada punto de soldadura y no se apaga
  por una bajada pasajera. Entre el valor normal y el umbral no hay cambio (histéresis).
- **Solo una lectura válida cambia el estado.** Si la sonda no da dato (lectura mala o sonda en falla), la causa
  queda como estaba: no prende ni apaga el aviso. Si pasan más de `ALARMA_HUECO_MAX_MS` (30 s) sin lecturas
  válidas, la cuenta del minuto en curso empieza de nuevo.
- **El ADC saturado cuenta como lectura válida y alta** ("al menos" 4083 mV): no corta el aviso al saturar ni al
  salir de la saturación.
- **El silencio es por causa y vence.** Dura hasta que esa causa se normaliza, o hasta que pasan 30 minutos
  (`ALARMA_SILENCIO_MAX_MS`): si para entonces sigue alta, vuelve a sonar. Callar el CO2 no calla el humo, y una
  causa nueva suena aunque otra esté silenciada.
- **Calentamiento por sensor.** El aviso de CO2 funciona desde el arranque (el SCD40 da lecturas a los pocos
  segundos); el de humo empieza cuando termina el calentamiento del MQ2.
- **Reinicio.** Tras un corte de luz todo arranca en normal; si la causa sigue, el aviso se prende al minuto.
- Cada cambio de estado de una causa se informa a AURA con una alerta (ver arriba) y se publica una muestra.

## Configuración

Parámetros que acepta `set_config` (contrato §3.3). Se valida todo antes de aplicar y se guarda en la
NVS.

| Parámetro | Rango | Valor en esta placa |
|---|---|---|
| `intervalo_s` | 5 a 86400 (entero) | 60 |
| `aula` | lo maneja la biblioteca | la asigna AURA; sin aula manda directo al raíz |

Comando propio: `{"command":"silence"}` pasa a "silenciada" las causas **activas en ese momento**, cada una hasta
que se normalice o pasen 30 minutos. **Sin causa activa se rechaza** (`rechazado`, motivo `sin_alarma`): no se
guarda "para después", así un `silence` justo antes de que se prenda el aviso no deja mudo el buzzer. Se acepta
desde AURA y desde el botón del laboratorio.

## Compilar y probar

Requiere ESP-IDF 6.1 (instalado con [EIM](https://dl.espressif.com/dl/eim/)), en una ruta sin
espacios ni tildes y fuera de Google Drive u OneDrive. **El CI todavía no compila los proyectos
ESP-IDF: compilalo antes de abrir un PR.**

1. Copiar `main/config_local.ejemplo.h` como `main/config_local.h` y completar el ID y la clave de la
   mesh (los da la cátedra). No se versiona. **No hace falta el WiFi del edificio**: solo el raíz se
   conecta al router.
2. Desde esta carpeta:
   ```bash
   idf.py set-target esp32c3
   idf.py -p <puerto> build flash monitor
   ```
3. Los primeros 2 minutos el monitor informa el calentamiento ("faltan N s") y las lecturas del SCD40 (el MQ2 figura
   "(calentando)"). Después se ve cada ciclo de 5 s: CO2, temperatura, humedad, MQ2, estado del aviso de cada causa, lecturas
   del SCD40 y muestras pendientes de confirmar. Al arrancar, el escaneo I2C tiene que listar el
   **SCD40 (0x62)**. El LED arranca en amarillo parpadeando; al conectarse a la mesh pasa a azul (calentando) y, terminados los 2 minutos, a verde.
4. Con la mesh de la cátedra: la placa aparece **pendiente** en AURA (cuarentena, sus datos se
   guardan y se confirman). Al aceptarla y asignarle el aula con `set_config {"aula": ...}`, pasa a
   `modo: aula`. El `hw_id` (`mac-...`) sale de la MAC de la placa: se le pasa a la cátedra para el alta.
   Sin backend, la cátedra usa el banco (`Mesh_Red_Completa/Broker_MQTT/`).
5. Tests sin placa (protocolo del SCD40, conversión del MQ2, alarma, estado de las sondas, calentamiento y colores del LED), que el CI
   corre solos: `make -C dispositivos/E1-PB-LECA-AMB01/tests`.
6. Conector: `python3 -m unittest discover -s conectores/E1-PB-LECA-AMB01/tests`.

## Notas

- **Calentamiento (2 min).** El MQ2 tiene un calefactor y el SCD40 necesita un rato para estabilizarse.
  Los dos arrancan juntos y comparten un solo reloj de `CALENTAMIENTO_S` = 120 s
  ([`main/calentamiento.h`](main/calentamiento.h)). Mientras corre: **no se publica nada**, no se informa
  el estado de las sondas y el aviso de humo/vapores (MQ2) queda **inhibido**; el de CO2 sigue funcionando. Al terminar se publica enseguida una medición con los valores de ambos sensores juntos
  (si uno falla, se publica el otro y la sonda caída se avisa, contrato §3.1/§3.5). El reloj es el
  tiempo desde el arranque: tras cualquier reinicio, incluido un corte de luz, el calentamiento vuelve
  a empezar completo, y el aviso de humo no funciona en ese lapso. 2 minutos alcanzan para estabilizar el SCD40 pero
  son poco para un MQ2 nuevo, que recomienda un precalentamiento mucho más largo (la primera vez, horas):
  hasta entonces `gas_mv` puede derivar y el umbral de alarma conviene calibrarlo con aire limpio.
- **Cola.** Las mediciones esperan en RAM (hasta 400) y se respaldan en la flash recién si pasan 5
  minutos sin confirmación de AURA (contrato §5). No hay nada que configurar: viene en la biblioteca.
- **Portado desde Arduino.** La lógica de medición, los umbrales, `intervalo_s` y `silence` partieron de
  la versión anterior; el aviso se rediseñó (estados por causa, filtro de 1 min, silencio con vencimiento). Ya no hay display (lo reemplaza el LED de estado) y
  que `MAC_ESPERADA` ya no existe: la identidad es la MAC de la placa.
- **Pendiente de probar en placa:** la lógica (alarma, sondas, conversión del MQ2, LED, calentamiento) tiene
  tests de host que corren en el CI. La primera versión de este nodo se compiló y firmó con ESP-IDF 6.1 (`esp32c3`);
  **tras el rediseño del aviso (estados por causa, filtro de 1 min, buzzer con pitidos) hay que volver a compilar**, y
  **todavía no se probó en la placa ni con la mesh real**.

## Actualización por la malla y firma (contrato v4.3 y v4.4)

- El tipo `aura-gas-co2` y la versión salen de `CMakeLists.txt`: **subir `PROJECT_VER` en cada entrega**.
- `partitions.csv` tiene dos lugares para el firmware (`ota_0` y `ota_1`, de 1,75 MB) y la partición `cola`,
  como `Mesh3_Hoja`. El firmware tiene que entrar en 1,75 MB: `idf.py build` lo informa al final.
- `sdkconfig.defaults` activa la vuelta atrás y la verificación de firma. **La compilación no firma**: la imagen se
  firma aparte (`GUIA_NODOS_HOJA.md`, sección 2.9) antes de grabarla por USB o de mandarla por OTA. Para pruebas
  propias se puede usar una clave de prueba propia (que queda fuera del repo). **El grupo no entrega ningún `.bin`
  para la instalación**: la cátedra compila y firma, con su clave, lo que sale de `main` tras el merge; la primera
  vez la placa se graba por USB con esa imagen firmada. Si ya tenías un `sdkconfig` anterior, borralo y volvé a compilar.
- No hay código propio para esto: lo resuelve la biblioteca. Lo único que exige a `main.c` es no trabarse.
