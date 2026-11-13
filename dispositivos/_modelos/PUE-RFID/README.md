# PUE-RFID — puerta con llavero de proximidad (modelo)

Firmware de las puertas del segundo piso del Edificio 2: abre con un llavero de proximidad, vigila
la puerta con un sensor magnético, se **arma** para detectar intrusiones y avisa a AURA. Es una
**hoja de la mesh** (ESP-IDF 6.1, [ADR-006](../../docs/adr/ADR-006-esp-wifi-mesh-reemplaza-a-esp-now-en-interior.md)
y [ADR-007](../../docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md)) y sigue las
decisiones del [ADR-009](../../docs/adr/ADR-009-puertas-con-llavero-la-puerta-decide-y-aura-supervisa.md)
(borrador, PR #42).

El mismo binario va en las nueve placas: la identidad es la MAC (`hw_id`) y lo propio de cada puerta
va por `set_config` y por la lista de llaveros que carga AURA. Las instalaciones son las carpetas
`E2-P2-*-PUE01` de [`dispositivos/`](../); cada una tiene solo su ficha.

| | |
|---|---|
| **Tipo de firmware** | `aura-puerta-rfid` ([ADR-008](../../docs/adr/ADR-008-actualizacion-por-la-malla-y-tipo-de-firmware.md)) |
| **Versión** | 0.1.0 (`PROJECT_VER` en [`CMakeLists.txt`](CMakeLists.txt)) |
| **Responsable** | losHijosDelStackOverFlow (IC IV 2026) |
| **Estado** | compila y tiene tests de host; **sin probar en placa ni en la mesh** |
| **Contrato** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.x. Los comandos `tarjeta_agregar`, `tarjeta_quitar` y `estado`, los eventos y el hash de la lista son los del ADR-009; **están a confirmar contra el contrato v4.5 que propone el ADR** |

## Hardware

| Componente | Modelo | Conexión (ESP32-C3 SuperMini) |
|---|---|---|
| Placa | ESP32-C3 SuperMini (4 MB) | — |
| Lector | RC522 (SPI) | SCK 6, MISO 10, MOSI 20, SS 21, RST 7 |
| Relé de la cerradura | módulo de 5 V. **Nivel bajo o alto según el módulo**: `CONFIG_PUERTA_RELE_ACTIVO_ALTO` (apagada = se activa con el pin en BAJO). Con la placa enchufada y sin hacer nada, **el LED del relé tiene que estar apagado**; si está prendido, la opción está mal elegida y la cerradura queda energizada en reposo | GPIO 5 |
| Cerradura | pestillo eléctrico Luber 500 (8–12 V, 5 W), fuente de 12 V aparte | relé: COM al + de la fuente, **NO** al + de la cerradura (fail-secure: trabada sin corriente); **diodo 1N400x en paralelo con la cerradura** (banda del lado +) |
| Sensor de puerta | MS14 (reed) entre el pin y GND, pull-up interna | GPIO 4 |
| Buzzer | pasivo (bobina), directo al pin | GPIO 3 |

Los pines están en [`main/pines.h`](main/pines.h). **El SCK pasó del GPIO 9 al GPIO 6**: el GPIO 9 es
el pin de BOOT de la C3 y si algo lo baja al arrancar, la placa entra en modo descarga. El GPIO 5 (relé)
no es de strapping. Hay que **recablear el SCK** respecto del prototipo de banco.

El sensor se cablea para que **un cable cortado se lea como "puerta abierta"** (contacto cerrado con el
imán, pull-up al pin): así un sabotaje del sensor no pasa inadvertido.

## Qué hace (casos de uso del anexo A del ADR-009)

La cerradura queda **trabada en reposo**. Se destraba solo con un **pulso** de corriente.

| | Comportamiento |
|---|---|
| U1 Llavero autorizado | Pulso (`pulso_ms`, con tope), pitido de OK (moneda) y registro. Si la puerta se abre, se corta el pulso; si nadie abre, traba al vencer |
| U2 Llavero desconocido | Dos pitidos graves y registro del rechazo **con el UID** (sirve para dar de alta) |
| U3 Llavero apoyado | Una sola lectura por cada vez que se apoya (la misma tarjeta se ignora 700 ms) |
| U4 Muchos rechazos | 5 en 60 s: se frenan las lecturas 30 s y se manda la alerta `intentos_rechazados` |
| U5 Salida por el picaporte | Desarmada: se registra como `apertura`, sin acceso ni alarma |
| U6 Puerta abierta de más | Pasados `aviso_s` desde que se abre: aviso sonoro que sube de volumen y de ritmo (**tope de 2 min**) y alerta `puerta_abierta`; al cerrarse, otra alerta `info` |
| U7 Armar | **Dos pasadas** de un llavero autorizado en ≤ 3 s, con la puerta **cerrada**: la segunda corta el pulso y traba; suenan tres notas bajando. Se guarda en NVS y se registra con el UID |
| U8 Armar con la puerta abierta | No arma: pitido de error |
| U9 Desarmar | **Una pasada** desarma y abre. Si se armó sin querer, la tercera pasada lo corrige. Con la sirena sonando, el llavero **corta la sirena y desarma** (la puerta ya está abierta, así que no da pulso) |
| U10 Intrusión | Armada, la puerta se abre sin desarmar: **la alerta `critical` es lo primero que sale**, luego el aviso local (tope 2 min) y el registro; al cerrarse, alerta `info`. Sigue armada |
| U11 Arranca armada y abierta | Alerta `arranque_puerta_abierta`, sin sonido |

Tiempos fijos: doble pasada 3 s, tope del aviso 2 min, tope del pulso **10 s**, límite de **20 s
energizada por cada minuto**. Armada, el latido es de 30 s; desarmada, de 120 s.

### Lo que NO tiene (a propósito)

Sin **modo emergencia** ni **libre paso** (dejan la cerradura energizada y la queman), sin **tarjeta
maestra** (una lista que se cambia en la puerta deja de coincidir con la de AURA), sin **enroladora por
ESP-NOW**, sin **horarios** (cada llavero habilitado abre su puerta las 24 h). Armar y desarmar se hace
**solo en la puerta**, nunca desde AURA.

## Qué envía

**Eventos** (`hw/<hw_id>/data`, con `ingest_id`: sin red quedan en la cola de la placa y no se pierden):

```json
{"evento":"acceso","uid":"ABE8B022","puerta":0,"armada":0}
```

`evento`: `acceso`, `rechazo`, `apertura`, `cierre`, `armado`, `desarmado`, `intrusion`. `uid` solo si lo
hubo; `origen:"app"` si vino de una orden remota. **Los `values` con texto son del ADR-009 (decisión 9) y
hay que confirmarlos contra el contrato.**

**Latido** (cada `intervalo_s`): `{"puerta":0,"armada":0,"cerradura":0}`.

**Status** (`details.config`): `intervalo_s`, `pulso_ms`, `aviso_s`, `sensor`, `tarjetas` (cantidad) y
`hash`. AURA compara la cantidad y el hash con los suyos; si no coinciden, alerta y recarga (A3).

**Alertas** (`hw/<hw_id>/alerts/<tipo>`): `intrusion` (critical), `puerta_abierta` (warning),
`puerta_cerrada` (info), `intentos_rechazados` (high), `limite_pulsos` (warning),
`arranque_puerta_abierta` (high) y, de la biblioteca, `sensor` con `campo` `lector` o `puerta`.

### Hash de la lista

Para que AURA lo calcule igual: UID en **hexa mayúsculas sin separadores**, ordenados de menor a mayor
como texto, unidos con `,`; **CRC-32 IEEE** (el de `zlib.crc32`) en 8 dígitos hexa **minúsculas**. La
lista vacía da `00000000`. Ej.: `{ABE8B022}` → `ad946105`. Detecta desfasajes, no es una firma.

## `set_config`

Lo que cambia de una puerta a otra va acá, con un valor por defecto común y no en `#define`. Se valida
todo o no se aplica nada, y se guarda en NVS **solo si cambió**.

| Parámetro | Rango | Por defecto | Para qué |
|---|---|---|---|
| `pulso_ms` | 500 – 10000 | 5000 | cuánto se destraba la cerradura. **Nunca pasa el tope duro de 10 s** |
| `aviso_s` | 5 – 600 | 15 | segundos con la puerta abierta antes del aviso |
| `sensor` | `"reed"`, `"boton"` | `"reed"` | `reed`: abierta = pin en alto (imán lejos o cable cortado). `boton`: abierta = pin en bajo (banco) |

## Comandos

| Comando | Params | Qué hace |
|---|---|---|
| `tarjeta_agregar` | `{"uid":"ABE8B022"}` | Agrega el llavero. UID de 4, 7 o 10 bytes en hexa; si no, `uid_invalido`. Lista llena (32): `lista_llena`. Si ya estaba, `aplicado` igual (los reintentos de AURA no fallan) |
| `tarjeta_quitar` | `{"uid":"ABE8B022"}` | Lo quita; si no estaba, `aplicado` igual |
| `estado` | — | Solo lectura: manda el reporte y un latido ya |
| `abrir` | `{"vence":<epoch s>}` | **Deshabilitado al compilar** (`abrir_deshabilitado`). Habilitado: exige vencimiento, hora válida y vigencia ≤ 5 min; desarma y abre |

Las órdenes locales (botón del concentrador) se rechazan salvo `estado`.

### Apertura remota (Kconfig)

`CONFIG_PUERTA_ABRIR_REMOTO` está **apagada**. Hasta que la etapa 2 de seguridad de la mesh lo resuelva, un
comando falso que pase como del raíz podría abrir cualquier puerta y una orden encolada se podría ejecutar
horas después. Habilitarla exige publicar una imagen nueva, que exige la firma de la cátedra.

## Alta de llaveros

1. `lector_banco/` (C3 + RC522 por USB) imprime `UID ABE8B022` al apoyar el llavero. Usa **el mismo
   código** que la puerta (`lector.c` y `lista_tarjetas.h`), así que el texto es el que la puerta compara.
2. Se asigna en AURA y AURA manda `tarjeta_agregar` a las puertas de esa persona; para la baja,
   `tarjeta_quitar` a todas, reintentando hasta que cada una confirma.
3. Una placa nueva arranca con la lista **vacía**: no abre con ningún llavero (F11).

**Sin gateway (solo banco):** `CONFIG_PUERTA_CARGA_POR_CONSOLA=y` agrega `agregar <UID>`, `quitar <UID>` y
`listar` por el USB. No tiene autenticación: **no se habilita en una puerta instalada**.

## Compilar, firmar y grabar

```
source ~/.espressif/tools/activate_idf_v6.1.sh      # o el export.sh de tu ESP-IDF 6.1
cp main/config_local.ejemplo.h main/config_local.h   # y completar el ID y la clave de la mesh (los da la cátedra)
idf.py build
```

Después se **firma** la imagen y se graba por USB, o se manda por OTA
([`GUIA_NODOS_HOJA.md`](../../Mesh_Red_Completa/GUIA_NODOS_HOJA.md), 2.8 y 2.9). Si había un `sdkconfig`
viejo, borrarlo. La imagen ocupa ~1,1 MB de los 1,75 MB de cada lugar OTA. El CI todavía no compila
ESP-IDF: compilar a mano antes de subir.

**Tests de host** (la lógica va en headers C puros, sin placa): `make -C tests`. Cubren la lista de
llaveros y su hash, el tope y el límite por minuto del pulso, la máquina de estados (U1 a U11, vigencia de
`abrir`, 100 días de uptime sin rollover) y los patrones de sonido.

## Cómo está hecho

| Archivo | Rol |
|---|---|
| `main/puerta_logica.h` | máquina de estados pura: recibe hechos con la hora y devuelve acciones |
| `main/cerradura_pulso.h` | tope del pulso y límite por minuto (ventana móvil de 60 s) |
| `main/lista_tarjetas.h` | UID, lista y hash (también lo usa el lector de banco) |
| `main/sonido_logica.h` | qué suena y cuándo |
| `main/latido_logica.h` | intervalo del latido (30 s armada, 120 s desarmada) y cuándo toca: se actualiza antes de cada reporte |
| `main/sensor_logica.h` | antirrebote del sensor y qué pasa si `set_config` cambia el tipo (se resincroniza, sin evento) |
| `main/lector_logica.h` | cuándo el lector está sano, hay que reintentar `rc522_start()` o reiniciarlo |
| `main/config_validacion.h` | entero dentro de rango, comparando antes de convertir |
| `main/rele.c` | **lo único que toca la cerradura**; su `esp_timer` la traba aunque la lógica se cuelgue |
| `main/sonido.c` | tarea propia: ningún sonido frena la lectura |
| `main/lector.c` | RC522 sobre `abobija/rc522` 4.0.1; revisa que lea y, si no, reintenta el arranque o lo reinicia |
| `main/almacen.c` | NVS, escribiendo solo si el valor cambia; la config va en un solo blob (todo o nada) |
| `main/main.c` | cablea todo y ejecuta las acciones en orden |

- **Tiempo de 64 bits** (`esp_timer_get_time()`): no hay rollover de `millis()` a los 49,7 días, y aun así se
  comparan diferencias con signo.
- **El relé es lo primero que se configura** en `app_main`, antes de NVS, el lector y la mesh. El pin sale
  directo en reposo (el nivel se escribe antes de pasarlo a salida).
- **Watchdog de tareas** (`CONFIG_ESP_TASK_WDT_PANIC=y`, 5 s) sobre la lógica y el sonido: si se cuelgan, la
  placa reinicia y el relé vuelve al reposo (trabada). Un lector que no vuelve en 5 min también reinicia.
- La alerta de intrusión es la primera acción de su tanda y el sensor se lee antes que las tarjetas.

## Pendientes y debilidades conocidas

**A decidir con la cátedra / la universidad**
- **Fail-safe o fail-secure.** Que la cerradura abra sin corriente puede ser lo correcto por evacuación, pero
  lo decide la universidad, no el firmware. **Este firmware supone una cerradura trabada sin corriente** (la
  del ADR-009).
- **Luber 500: fail-secure, a confirmar con su etiqueta.** Un texto sobre la Luber 500 (sin fuente citada) la
  da como fail-secure y alimentada con **8–12 V de corriente ALTERNA, 1 A**, y coincide con lo visto en banco
  el 2026-11-06: el módulo de relé del banco es de nivel alto, así que el prototipo la dejaba energizada en
  reposo y con la lógica "al revés". Con `CONFIG_PUERTA_RELE_ACTIVO_ALTO=y` queda trabada en reposo y se
  destraba en el pulso. **Falta confirmarlo con la etiqueta o la hoja de datos**, sobre todo el tipo de
  corriente: si es de alterna, alimentarla con una fuente de **continua** funciona para un pulso corto pero
  **nunca debe quedar energizada** (la bobina de un destrabapestillo de alterna sobre continua consume más y
  se quema); por eso importan el tope de 10 s y el límite por minuto.
  Nota de historia: durante el banco se llegó a creer que era fail-safe por el relé invertido. Si una
  cerradura fuera de verdad fail-safe, habría que revisar las decisiones 1 y 2 del ADR.
- **Tope del pulso.** Hay 10 s duros (el valor del ADR cuando la hoja de datos no dice otra cosa) y 20 s por
  minuto. Falta la hoja de datos o **medir cuánto aguanta energizada sin recalentarse** (`PULSO_TOPE_MS` y
  `LIMITE_MINUTO_MS` en `cerradura_pulso.h`).
- Retención del registro y quién lo ve; quién recibe las alertas de intrusión.

**Debilidades conocidas**
- **Repetición de órdenes.** La puerta no valida secuencia: una trama grabada se puede reenviar. Con `abrir`
  deshabilitada afecta solo a `tarjeta_agregar/quitar`; la mesh con seguridad (etapa 2) lo reemplaza.
- **El control es solo por UID**, que se clona fácil en MIFARE Classic, y los intentos están limitados solo
  por el bloqueo de 30 s tras 5 rechazos. Aceptable para el prototipo.
- El bloqueo por rechazos permite que alguien frene el lector a propósito: la llave mecánica sigue andando.
- Un corte de luz deja la puerta trabada pero **sin registro**: se entra con la llave y no queda constancia.
- Un relé con el contacto pegado no se detecta con este hardware (F5): se mitiga con la compra.
- La cola de eventos de la biblioteca es de 8: una ráfaga de alertas con la mesh caída puede perder las más viejas.
- Si el lector se reinicia por alimentación (en banco pasaba cada ~5 s), la placa lo vuelve a levantar sola
  sin avisar; solo avisa si no vuelve.
- La doble pasada arma al instante (sin cuenta para salir): se arma desde afuera, con la puerta cerrada.
  Armar con alguien adentro da falsa alarma.

## Qué cambió respecto del prototipo en banco (PR #40)

- **Se sacó:** modo emergencia, tarjeta maestra (su UID estaba publicada en el repo: esa tarjeta física está
  quemada), enroladora por ESP-NOW, sirena sin tope, cuenta de 10 s para salir.
- **Se invirtió:** el relé en reposo deja la cerradura **trabada**.
- **Se conservó:** pitidos, aviso de puerta abierta, armado con doble pasada, detección del lector caído.
- **Se agregó:** tope y límite por minuto, lista administrada por AURA con hash, registro con `ingest_id`,
  latido más seguido armada, bloqueo por rechazos, `set_config`, `estado`, validación del UID (4/7/10 bytes),
  NVS solo si cambia, vencimiento de `abrir`, sonido en su tarea, watchdog.
- **Corregido:** el rollover de `millis()`, el arranque que abría la puerta, el `delay()` del sonido y la
  ficha que decía que la tarjeta "abre" con la sirena (ahora ficha y código dicen lo mismo).
