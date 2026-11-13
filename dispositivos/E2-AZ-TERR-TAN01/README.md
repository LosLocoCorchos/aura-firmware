# E2-AZ-TERR-TAN01 — nivel de tres tanques de agua de la terraza

Medición del nivel de agua de tres tanques, con una sola placa y tres sensores ultrasónicos.

| | |
|---|---|
| **Ubicación** | Edificio 2, azotea (terraza), sobre los tres tanques de agua |
| **Responsable** | Grupo de IC IV 2026 (`@forzanijuan22`, `@AlejoRacca`, `@Agucesano`) |
| **Estado** | en desarrollo |
| **Instalado** | todavía no |
| **Transporte** | LoRaWAN (exterior), clase A, según [ADR-003](../../docs/adr/ADR-003-dos-transportes-mesh-interior-lorawan-exterior.md) |
| **Contrato que implementa** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.4 (la parte LoRaWAN, igual desde la v4.0) |
| **Basado en** | ninguno (el firmware parte de `nodo_lorawan.ino` de `IC-lorawan-test`; los puertos y los comandos siguen el codec de [`E1-AZ-TERR-TAN01`](../E1-AZ-TERR-TAN01/)) |
| **Códigos anteriores** | ninguno |

## Qué hace

Mide la distancia del sensor a la superficie del agua en cada uno de los tres tanques y la manda
por LoRaWAN. Más agua significa menos distancia. El conector
([`conectores/E2-AZ-TERR-TAN01/`](../../conectores/E2-AZ-TERR-TAN01/)) guarda las mediciones y avisa
cuando un tanque queda bajo o un sensor deja de medir.

## Hardware

| Componente | Modelo | Conexión |
|---|---|---|
| Placa | Seeed XIAO ESP32S3, `hw_id` `eui-<DevEUI>` (el DevEUI del nodo) | — |
| Radio | Wio-SX1262, región AU915, subbanda 2 | pines propios del módulo |
| Sensor del tanque 1 | AJ-SR04M, alimentado a 5 V | TRIG `D0` directo; ECHO a `D1` por un divisor 1 kΩ / 2 kΩ |
| Sensor del tanque 2 | AJ-SR04M, alimentado a 5 V | TRIG `D2` directo; ECHO a `D3` por un divisor 1 kΩ / 2 kΩ |
| Sensor del tanque 3 | AJ-SR04M, alimentado a 5 V | TRIG `D4` directo; ECHO a `D5` por un divisor 1 kΩ / 2 kΩ |
| Fuente | módulo AC-DC de 220 V a 5 V, 700 mA, con fusible de 0,5 A en la fase, en caja estanca | salida al bus de 5 V (pin 5V de la placa y VCC de los sensores) y al GND común |
| Tanques | 3 × Mayper de polietileno de 5000 L: 2380 mm de alto hasta la tapa, 1770 mm de diámetro, boca de 590 mm (folleto del fabricante) | el sensor va en la tapa |

**Alimentación.** El nodo no duerme (mide, escucha comandos y transmite todo el tiempo): necesita
la red de 220 V, no anda a batería. Consume unos 100 mA con picos de unos 300 mA al transmitir.
Los AJ-SR04M van a 5 V, y su ECHO sale a 5 V: cada uno entra al GPIO de 3,3 V por un divisor de
1 kΩ (del ECHO al pin) y 2 kΩ (del pin a GND), que deja 3,3 V. El TRIG va directo (3,3 V alcanza).
Para programar por USB, desconectar la fuente: el pin 5V de la placa está unido al USB.

## Lo que envía a AURA

Cada medición es un campo de `values` (contrato §3.1).

| Campo | Unidad | Rango válido | Cuándo |
|---|---|---|---|
| `distancia_tanqueN_cm` (N = 1, 2, 3) | cm, con un decimal | 25 a 600 | cada `intervalo_s` (300 s por defecto), los tres juntos; y al pedir `medir_ahora` |
| `nivel_tanqueN_pct` | %, con un decimal | 0 a 100 | junto con la distancia |

**Mide los tres y manda.** Cada `intervalo_s` se miden los tres tanques, uno detrás del otro
(menos de 2 s), y enseguida sale un mensaje con los tres. Los ciclos se cuentan desde el primero,
sin deriva.

La distancia de cada tanque es la mediana de 5 lecturas. Si el sensor no responde o mide más
lejos que el rango, ese tanque **no va en el mensaje**: es una falla, y el conector avisa `sensor`
cuando el tanque deja de llegar mientras los otros siguen llegando.

**Si no mide ninguno** (por ejemplo, se cortó el cable de 5 V de los sensores), el nodo manda
igual un mensaje de estado, con la falla de cada sensor, que el codec traduce a `errors`: así no
queda mudo, ChirpStack lo sigue viendo y, como es clase A, sigue recibiendo comandos.

**Ese estado no llega a AURA.** Cuando el codec devuelve solo `errors`, ChirpStack lo anota en el
registro de eventos del dispositivo y el bridge publica `status` `online` sin `data`. El nodo no
queda `offline` (bien), pero **en AURA nadie ve que se cayeron los tres sensores**: no hay
mediciones nuevas, no hay alerta (el conector tampoco avisa `sensor`, porque no llega ningún
mensaje con los otros tanques) y el nodo figura en línea. Solo se ve en ChirpStack. Es una
limitación conocida del contrato ([pendiente 19](../../docs/CONTRATO_MQTT.md)): no esperar una
alerta en AURA por esto.

**Precisión.** La distancia sale del tiempo del eco con la velocidad del sonido a unos 20 °C
(343 m/s), fija. En la azotea, entre 0 y 45 °C, eso da un error de hasta unos ±7 cm a 1,5 m
(unos ±4 %). Para el aviso de tanque bajo alcanza; si hiciera falta más precisión, habría que
compensar con un sensor de temperatura.

**Tanque lleno.** El sensor va en la tapa y el AJ-SR04M no mide a menos de unos 20 cm (zona
ciega); el firmware usa 25 cm, con margen. Si el agua llega a esa zona (al menos 3 de las 5
lecturas con eco más corto que 25 cm), el firmware manda `0xFFFE` y
el codec lo informa como **25 cm**, el nivel máximo que el sensor puede medir. No es una falla
sino la saturación del rango, como un transmisor 4-20 mA en su tope: `25` quiere decir "25 cm
o menos". No dispara la alerta `sensor` ni `tanque_bajo`.

**Nivel.** Con la altura y el lleno de cada tanque (ver *Comandos*), el nodo calcula
`nivel = (altura − distancia) / (altura − lleno)`, recortado a 0..100 %. Si a un tanque se le
saca la altura o el lleno (con 0 o `null`, ver *Comandos*), manda solo la distancia.

**Sin calibrar igual da nivel.** Con los valores de fábrica (ver *Comandos*) el nodo ya manda un
nivel. Si un sensor no queda en la tapa, o el tanque no es el Mayper de 5000 L, ese porcentaje
parece creíble pero está mal. Por eso la instalación incluye medir la altura real de cada tanque
y mandarla por `set_config` (ver *Instalación*).

Alertas (contrato §3.5). El nodo LoRaWAN no publica alertas propias: las genera el conector.
- `sensor`: un tanque falta en 3 mensajes seguidos que traen a los otros (3 ciclos, sea cual sea
  el intervalo); `info` cuando vuelve. Si fallan los tres a la vez no hay aviso (ver arriba).
- `tanque_bajo`: `warning` cuando el nivel baja del 20 % e `info` cuando vuelve a 25 % o más.
  La bomba la maneja un control externo: el aviso sirve para saber
  que no llenó a tiempo.

## Comandos

Llegan por el `lorawan-bridge` (contrato §3.3), por downlink en el fPort 10. El nodo contesta
cada uno con un reporte en el fPort 2 que lleva el `ref` del comando, el resultado y la
configuración vigente: el bridge lo publica como `aplicado` o `rechazado` (§3.4).

| Comando | Bytes | Qué hace |
|---|---|---|
| `medir_ahora` | `01 ref` | mide los tres tanques ya (sin mover el ciclo) y los manda, enseguida del reporte |
| `set_config` solo con `intervalo_s` | `02 ref hi lo` | cambia el intervalo (4 bytes: entra con `aura_max_downlink_bytes = 4`) |
| `set_config` con altura o lleno | `02 ref` + una entrada `id hi lo` por parámetro | cambia los parámetros de abajo |

| Parámetro de `set_config` | Rango | Qué es | Valor en esta placa |
|---|---|---|---|
| `intervalo_s` | 60 a 43200 s, entero | cada cuánto se miden los tres tanques y se manda; el ciclo arranca de nuevo con el intervalo nuevo | 300 |
| `altura_tanqueN_cm` | 25 a 600 cm, un decimal, o `null` / 0 | distancia del sensor (en la tapa) al **fondo** del tanque: lo que mide vacío (0 %). `cero_tanqueN_cm` es otro nombre del mismo parámetro. `null` o 0 lo deja sin altura (sin nivel) | 238 (Mayper 5000 L) |
| `lleno_tanqueN_cm` | 25 a 600 cm, un decimal, o `null` / 0 | distancia del sensor al agua con el tanque **lleno** (100 %). `null` o 0 lo deja sin lleno (sin nivel) | 35 (Mayper 5000 L) |

Todo se guarda en memoria no volátil. La altura tiene que ser al menos 10 cm mayor que el
lleno (si los dos están definidos). Se valida todo el comando antes de aplicar: si algo no valida, no cambia nada y se contesta
con el motivo (`fuera_de_rango`, `calibracion_invalida` o `comando_desconocido`).

**Valores de fábrica.** Sin nada guardado, el nodo usa el tanque Mayper de 5000 L: 238 cm de
altura (alto total hasta la tapa) y 35 cm de lleno (5000 L en un cilindro de 177 cm de diámetro
llegan a unos 203 cm del fondo: 238 − 203 = 35 cm, fuera de la zona ciega del sensor). Son
estimaciones del folleto: conviene ajustarlas una vez instalado el nodo.

**Cambiar la altura de los tanques** (si se cambian por otros, o para ajustar a la medida real):
`set_config {"altura_tanque1_cm": 245}`, o los tres juntos, con el lleno si cambia:
`set_config {"altura_tanque1_cm": 245, "lleno_tanque1_cm": 40, "altura_tanque2_cm": 245, ...}`.
Cómo medirlas: ver *Instalación*. Para volver a los de fábrica, mandarlos explícitos (`238` y
`35`).

**Clase A**: el nodo solo escucha después de transmitir, así que un comando llega con el próximo
mensaje: hasta `intervalo_s` después de encolarlo (5 min con 300 s; 12 h con el máximo).

**Gateway compartido.** Cada uplink es confirmado, así que cada uno es un ACK de bajada en el
gateway: con 300 s son 288 por día (con 60 s serían 1440). Por eso el intervalo por defecto es
300 s; para tanques de agua alcanza, y `medir_ahora` sirve si hace falta un dato ya.

## La red

- **Join**: si falla, reintenta con espera creciente (30 s, 1, 2, 4 y 5 min, con algo de azar) y
  sigue midiendo mientras tanto.
- **Se recupera sola**: si la red deja de confirmar 12 uplinks seguidos (por ejemplo, la cátedra
  reseteó la sesión o los nonces, borró y volvió a crear el device o le cambió el device
  profile), el nodo descarta su sesión y hace un join nuevo. Los DevNonce están en memoria no
  volátil y no se repiten: no hace falta resetearlos en ChirpStack después de reflashear (mientras
  no se borre la flash entera).

## Codec

[`codec.js`](codec.js) es el codec del device profile `aura-tanque-3x-ultrasonico` de ChirpStack
(antes `aura-clase-a`). Un cambio se
hace primero acá y después se copia al profile. Si cambia el formato de los bytes, cambian el
firmware y el codec a la vez. Los tests están en [`tests/`](tests/) (`make -C tests`, con Node).

### Subida

| fPort | Bytes | Qué es |
|---|---|---|
| 1 | 13 con los 3 tanques (9 o 5 si falta alguno) | Medición: máscara de tanques (bit 0 = tanque 1) y, por cada uno, distancia en mm (cm × 10) y nivel en décimas de % (`0xFFFF` = sin altura/lleno), uint16 big endian. Distancia `0xFFFE` (zona ciega, tanque lleno) = 25 cm |
| 1 | 4, máscara 0 | Estado: ningún sensor midió. Un byte por tanque con la falla (1 sin eco, 2 demasiado lejos). El codec devuelve `errors`, sin `values` |
| 2 | 16 | Reporte de un comando: `ref`, resultado (0 aplicado, 1 fuera de rango, 2 comando desconocido, 3 calibración inválida), `intervalo_s`, altura y lleno de cada tanque en mm (`0xFFFF` = sin definir: `null` en `config`) |

### Bajada (fPort 10)

| Comando | Bytes |
|---|---|
| `medir_ahora` | `01 ref` |
| `set_config` solo con `intervalo_s` | `02 ref hi lo` (s) |
| `set_config` con altura o lleno | `02 ref`, y por parámetro: `01 hi lo` (`intervalo_s`, s), `1N hi lo` (altura del tanque N, mm; 0 = sin altura), `2N hi lo` (lleno del tanque N, mm; 0 = sin lleno) |

### Device profile

| Campo | Valor |
|---|---|
| Nombre | `aura-tanque-3x-ultrasonico` |
| Codec | el contenido de [`codec.js`](codec.js) |
| Región, versión | AU915 subbanda 2, LoRaWAN 1.0.x, OTAA, clase A |
| *Uplink interval* | 43200 s, el máximo de `set_config` (el bridge infiere `offline` a 3 × ese valor). Como el nodo manda aunque fallen los sensores, solo queda mudo si se queda sin alimentación o sin red |
| Tag `aura_max_downlink_bytes` | `23` (un `set_config` con los 7 parámetros). Con `4` anda el intervalo y `medir_ahora`, pero no los cambios de altura o lleno |

## Compilar y probar

1. Copiá `config_local.h.example` como `config_local.h` (en esta carpeta) y completá `JOIN_EUI`,
   `DEV_EUI` y `APP_KEY`, que da la cátedra. `config_local.h` está en el `.gitignore`: **no se
   versiona**. Sin ese archivo el sketch compila, pero no se une a la red.
2. Placa: `esp32:esp32:XIAO_ESP32S3` (`fqbn.txt`), core ESP32 3.3.11. Bibliotecas: las de
   `bibliotecas.txt` (`RadioLib`); `Preferences` viene con el core.
3. En banco: el monitor serie muestra cada lectura (con el nivel), cada envío,
   si el servidor confirmó y cada comando que llega.
4. **No borrar la flash** al cargar el firmware (en el IDE, *Erase All Flash Before Sketch Upload*
   en *Disabled*): ahí están los DevNonce, el intervalo y la altura y el lleno de los tanques.

## Instalación

Hasta completar los pasos 2 y 3, el nivel que llega a AURA sale del folleto y no hay que confiar
en él.

1. Montar cada sensor en la tapa de su tanque, mirando derecho hacia abajo, sin que el haz toque
   la pared ni la entrada de agua.
2. Medir con una cinta la **altura real** de cada tanque: desde la cara del sensor hasta el fondo.
3. Mandarla por `set_config`, las tres juntas:
   `set_config {"altura_tanque1_cm": …, "altura_tanque2_cm": …, "altura_tanque3_cm": …}`.
   El reporte tiene que volver `aplicado`, con esas alturas en `config`.
4. Con un tanque lleno (cuando la bomba termina de llenar), pedir `medir_ahora`: la distancia que
   llega es el lleno de ese tanque. Si difiere de 35 cm, mandarlo con `lleno_tanqueN_cm`.
5. Pedir `medir_ahora` y comprobar que el nivel de cada tanque coincide con lo que se ve.

## Notas

- Pendiente: probar en la placa y en ChirpStack.
- Pendiente: la instalación (ver *Instalación*), con la altura y el lleno reales de cada tanque
  (los de fábrica salen del folleto del Mayper de 5000 L).
- Si fallan los tres sensores, AURA no se entera: solo se ve en ChirpStack (pendiente 19 del
  contrato).
