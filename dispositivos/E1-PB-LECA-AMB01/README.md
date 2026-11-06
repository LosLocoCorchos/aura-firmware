# E1-PB-LECA-AMB01 — nodo de gas y CO2 del LabECA

Mide CO2, temperatura y humedad (SCD40) y la concentración de gas combustible (MQ2) en el aire del
laboratorio. Un LED RGB indica el estado de la conexión a la mesh y un buzzer local suena cuando se
pasa un umbral, sin depender de la mesh.

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
Después, cada 5 s mide y evalúa la alarma local; cada
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
| Azul que "respira" | calentando sensores (2 min) |
| Magenta parpadeando rápido | alarma de gas o CO2 (prioridad sobre todo) |
| Cian, dos destellos cada 2 s | un sensor no da medición válida; entre destellos se ve el color de la conexión |

Prioridad: alarma > calentando > falla de sensor > conexión.

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
| `gas_mv` | mV | 0 a 5200 | MQ2 | `intervalo_s` |

`gas_mv` es la **tensión en el AO del MQ2**, ya compensada por el divisor: **no es ppm**. Pasarlo a
ppm requiere calibrar Ro del sensor en aire limpio y usar la curva del fabricante.

Alertas que emite el nodo (contrato §3.5): `sensor` cuando una sonda deja de responder
(`sin_respuesta`, para el SCD40) o da un valor fuera del rango del ADC (`fuera_de_rango`, para el
MQ2), una vez por cambio, y otra con `motivo: "recuperada"` cuando vuelve.

Las alertas por **umbral** las emite el conector dentro de AURA (`dioxido_carbono_alto`, `gas_alto`):
ver [`conectores/E1-PB-LECA-AMB01/`](../../conectores/E1-PB-LECA-AMB01/).

## Alarma local

Independiente de la mesh y de AURA. Suena el buzzer cuando `gas_mv >= 2400` o `co2_ppm >= 1500`. Esos umbrales son constantes del firmware
(`GAS_ALARMA_MV` y `CO2_ALARMA_PPM`, en [`main/alarma.h`](main/alarma.h)) y **hay que ajustarlos
tras calibrar**; el conector usa los mismos valores, y si se cambian en uno hay que cambiarlos en el
otro.

## Configuración

Parámetros que acepta `set_config` (contrato §3.3). Se valida todo antes de aplicar y se guarda en la
NVS.

| Parámetro | Rango | Valor en esta placa |
|---|---|---|
| `intervalo_s` | 5 a 86400 (entero) | 60 |
| `aula` | lo maneja la biblioteca | la asigna AURA; sin aula manda directo al raíz |

Comando propio: `{"command":"silence"}` calla el buzzer hasta que la alarma se normalice.

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
3. Los primeros 2 minutos el monitor solo informa el calentamiento ("faltan N s"). Después
   se ve cada ciclo de 5 s: CO2, temperatura, humedad, gas, estado de la alarma, lecturas
   del SCD40 y muestras pendientes de confirmar. Al arrancar, el escaneo I2C tiene que listar el
   **SCD40 (0x62)**. El LED arranca en amarillo parpadeando y pasa a verde al conectarse a la mesh.
4. Con la mesh de la cátedra: la placa aparece **pendiente** en AURA (cuarentena, sus datos se
   guardan y se confirman). Al aceptarla y asignarle el aula con `set_config {"aula": ...}`, pasa a
   `modo: aula`. El `hw_id` (`mac-...`) sale de la MAC de la placa: se le pasa a la cátedra para el alta.
   Sin backend, la cátedra usa el banco (`Mesh_Red_Completa/Broker_MQTT/`).
5. Tests sin placa (protocolo del SCD40, conversión del MQ2, alarma, calentamiento y colores del LED), que el CI
   corre solos: `make -C dispositivos/E1-PB-LECA-AMB01/tests`.
6. Conector: `python3 -m unittest discover -s conectores/E1-PB-LECA-AMB01/tests`.

## Notas

- **Calentamiento (2 min).** El MQ2 tiene un calefactor y el SCD40 necesita un rato para estabilizarse.
  Los dos arrancan juntos y comparten un solo reloj de `CALENTAMIENTO_S` = 120 s
  ([`main/calentamiento.h`](main/calentamiento.h)). Mientras corre: **no se publica nada**, no se informa
  el estado de las sondas, la alarma y el buzzer quedan **inhibidos**. Al terminar se publica enseguida una medición con los valores de ambos sensores juntos
  (si uno falla, se publica el otro y la sonda caída se avisa, contrato §3.1/§3.5). El reloj es el
  tiempo desde el arranque: tras cualquier reinicio, incluido un corte de luz, el calentamiento vuelve
  a empezar completo, y en ese lapso no hay alarma. 2 minutos alcanzan para estabilizar el SCD40 pero
  son poco para un MQ2 nuevo, que recomienda un precalentamiento mucho más largo (la primera vez, horas):
  hasta entonces `gas_mv` puede derivar y el umbral de alarma conviene calibrarlo con aire limpio.
- **Cola.** Las mediciones esperan en RAM (hasta 400) y se respaldan en la flash recién si pasan 5
  minutos sin confirmación de AURA (contrato §5). No hay nada que configurar: viene en la biblioteca.
- **Portado desde Arduino.** La lógica de medición, los umbrales, `intervalo_s` y `silence` son los de
  la versión anterior. Cambia que ya no hay display (lo reemplaza el LED de estado) y
  que `MAC_ESPERADA` ya no existe: la identidad es la MAC de la placa.
- **Pendiente de probar en placa:** esta versión se verificó con tests y una simulación de la lógica,
  pero todavía no se compiló ni se probó con ESP-IDF 6.1 ni con la mesh real.

## Actualización por la malla y firma (contrato v4.3 y v4.4)

- El tipo `aura-gas-co2` y la versión salen de `CMakeLists.txt`: **subir `PROJECT_VER` en cada entrega**.
- `partitions.csv` tiene dos lugares para el firmware (`ota_0` y `ota_1`, de 1,75 MB) y la partición `cola`,
  como `Mesh3_Hoja`. El firmware tiene que entrar en 1,75 MB: `idf.py build` lo informa al final.
- `sdkconfig.defaults` activa la vuelta atrás y la verificación de firma. **La compilación no firma**: la imagen se
  firma aparte, con la clave de la cátedra, antes de grabarla por USB o de mandarla por OTA
  (`GUIA_NODOS_HOJA.md`, sección 2.9). Para pruebas propias se puede usar una clave de prueba. Si ya tenías un
  `sdkconfig` anterior, borralo y volvé a compilar.
- No hay código propio para esto: lo resuelve la biblioteca. Lo único que exige a `main.c` es no trabarse.
