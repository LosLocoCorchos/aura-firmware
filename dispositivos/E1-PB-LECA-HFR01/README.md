# E1-PB-LECA-HFR01 — Monitoreo de heladera y freezer

| | |
|---|---|
| **Ubicación** | Propuesta: Edificio 1, planta baja, LabECA; confirmar instalación. |
| **Responsable** | Grupo de IC IV 2026: @VainstubTomas, @Bernidb, @Kinggrass265, @JuanCruz46. |
| **Estado** | Firmware portado a ESP-IDF; pendiente la validación física con la C3. |
| **Instalado** | Todavía no. |
| **Transporte** | Interior: hoja ESP-WIFI-MESH; no reenvía tráfico de otras placas. |
| **Contrato** | [Copia MQTT v4.4 (propuesta)](../../docs/CONTRATO_MQTT.md), [ADR-007](../../docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md) y [ADR-008](../../docs/adr/ADR-008-actualizacion-por-la-malla-y-tipo-de-firmware.md). |
| **Basado en** | [Mesh3_Hoja](../../Mesh_Red_Completa/Mesh3_Hoja/) y su biblioteca pública `nodo_mesh`. |
| **Firmware** | Tipo `aura-heladera-freezer`, versión `1.0.2`; mismo binario para las placas del mismo uso. |
| **Códigos anteriores** | Ninguno confirmado. |

## Qué hace

Una **ESP32-C3 SuperMini** lee dos sondas DS18B20: una de heladera y otra de freezer.
Cada una tiene su intervalo y sus umbrales. El LED de alarma se enciende
si una sonda registra dos lecturas válidas seguidas del mismo lado del rango,
incluso sin conexión. Una pantalla SH1106 muestra
temperaturas y pendientes; el DS3231 con pila permite conservar UTC sin red.

La biblioteca de la cátedra administra la conexión, el concentrador de aula, la
cola híbrida y los ACK de AURA. El dispositivo usa exclusivamente su API pública:
no accede a variables internas ni a `esp_wifi_*` o `esp_mesh_*`.
El conector Python de [conectores/E1-PB-LECA-HFR01](../../conectores/E1-PB-LECA-HFR01/)
valida y guarda mediciones, y genera alertas al cruzar umbrales.

## Organización del código

| Archivo o carpeta | Responsabilidad |
|---|---|
| `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv` | Proyecto ESP-IDF 6.1, C3, tipo/versión, dos particiones OTA, rollback y cola de 256 KiB. |
| `main/main.c` | Arranque y tarea de medición; intervalos independientes y API de AURA. |
| `main/hardware.h` | Propuesta de GPIO y opciones de pantalla, RTC y entrada de alimentación. |
| `main/config_local.ejemplo.h` | Plantilla del ID y la clave privados de la mesh. |
| `main/logica.c/.h` | Rangos, alarma con histéresis, CRC y calendario UTC. |
| `main/config_parser.c`, `main/configuracion.c/.h` | Parche JSON, validación completa, aplicación y reporte de configuración. |
| `main/almacenamiento.c/.h` | NVS de configuración y último estado de alimentación. |
| `main/sondas.c/.h` | Dos buses 1-Wire, detección y lectura de DS18B20 con el controlador de Espressif. |
| `main/bus_i2c.c/.h` | Bus I2C compartido por OLED y RTC. |
| `main/reloj.c/.h` | DS3231, validación de la hora y reloj de sistema. |
| `main/pantalla.c/.h` | SH1106 128×64 y LEDs externos de umbral y desconexión. |
| `main/energia.c/.h` | Antirrebote y alerta por cambio de alimentación. |
| `main/idf_component.yml`, `dependencies.lock` | Dependencias declaradas y versiones resueltas por ESP-IDF. |
| `tests/` | Pruebas de lógica y configuración en computadora; no se cargan en la placa. |

## Hardware

Los siguientes GPIO son una **propuesta nueva para la C3**, no una equivalencia
de los D0…D5 de la XIAO. Confirmar cableado y niveles antes de energizar.

| Componente | Conexión inicial en `main/hardware.h` |
|---|---|
| ESP32-C3 SuperMini | Flash de 4 MiB; placa provista por el proyecto. |
| DS18B20 heladera | GPIO3, bus independiente con pull-up externo de 4,7 kΩ a 3,3 V. |
| DS18B20 freezer | GPIO4, otro bus con su pull-up de 4,7 kΩ a 3,3 V. |
| LED umbral, externo con resistencia | GPIO7, activo alto. |
| LED desconexión, externo con resistencia | GPIO10, activo alto. |
| OLED SH1106 128×64 | SDA GPIO8, SCL GPIO9; dirección 0x3c. |
| RTC DS3231 con pila | Mismo I2C, dirección 0x68; hora guardada en UTC. |
| Detección de alimentación | Deshabilitada (`HFR_PIN_ALIMENTACION = -1`) hasta confirmar el circuito. |

Reservar GPIO18/19 para USB y GPIO2 para arranque. GPIO8 y GPIO9 se usan para
I2C y también intervienen en el arranque. El bus usa pull-ups a 3,3 V y
ningún módulo debe mantener SCL (GPIO9) en bajo al reiniciar.

Las sondas se usan con alimentación externa, sin modo parásito. La resolución
es de 9 bits (0,5 °C) y cada conversión ocupa unos 100 ms de la tarea de medición.

La alarma se recupera al entrar 0,5 °C en el rango desde el umbral que se
cruzó. Para rangos menores a 1 °C, el margen es la mitad de su ancho. Si una
sonda falla, el LED de temperatura conserva el estado de su alarma. Una falla
por sí sola se indica en pantalla y con la alerta `sensor` en AURA.

El LED de desconexión indica **asociación a la mesh**, según la API pública.
No distingue por sí solo una caída del broker o de AURA: se observa también la
cola de pendientes. Tras 15 s sin mesh, o al detectar batería, se apaga la OLED
para ahorrar energía; medir y evaluar umbrales continúa. No se usa deep sleep,
porque la hoja debe permanecer en la mesh.

## Lo que envía a AURA

La identidad es `hw_id = mac-<MAC de fábrica>`. El raíz publica en
`hw/<hw_id>/data`; el firmware no contiene UUID, aula, MAC del padre ni datos
del broker. AURA asigna el dispositivo y el aula después del alta. Todas las
placas de este tipo llevan el mismo binario y parámetros de mesh.

| Campo de `values` | Unidad | Rango válido | Intervalo inicial |
|---|---|---|---|
| `temp_heladera_c` | °C | -55…125, excepto el centinela 85 | 60 s |
| `temp_freezer_c` | °C | -55…125, excepto el centinela 85 | 300 s |

Se omiten lecturas fallidas, no finitas, -127 y 85; no se envían valores ficticios,
`null` ni diagnósticos entre las temperaturas. Cuando vencen ambos intervalos,
las dos lecturas válidas se incluyen en una muestra. En otros ciclos solo se
envía la sonda que corresponde medir. El JSON queda por debajo de los 160 bytes
permitidos para `values`.

Las muestras se encolan inmediatamente tras medir; `nodo_mesh` asigna la hora
y el `ingest_id`, y las libera con la confirmación de AURA. Si hay un RTC válido,
se carga UTC al arrancar; la mesh ajusta el reloj. Sin RTC ni hora de la mesh,
no se inventa una fecha: se aplica el mecanismo de uptime de la biblioteca.

Falla y recuperación de sonda usan `nodo_mesh_sonda()` (alerta `sensor` por
transición). Los cambios de alimentación usan `energia` con
`details.alimentacion = "red" | "bateria"`. Las alertas de temperatura las
genera el conector; el LED de umbral se resuelve localmente.

## Configuración remota

`set_config` valida el parche completo antes de modificar nada. Solo informa
éxito si la escritura y el commit en NVS terminan bien. Un rechazo conserva
los ajustes anteriores. Arranca con valores iniciales si no hay configuración
válida guardada. Si al arrancar la NVS está llena o tiene una versión
incompatible, se borra y se reinicializa como en `Mesh3_Hoja`. Se pierden los
ajustes de esa partición, incluida el aula asignada. Los demás errores
se informan por el monitor serie.

| Parámetro | Significado y rango | Inicial |
|---|---|---|
| `intervalo_s` | Heladera: entero 5…86400 s. También lo usa el raíz para inferir desconexión. | 60 |
| `f_s` | Freezer: entero 5…86400 s. | 300 |
| `hlo`, `hhi` | Mínimo y máximo de heladera, °C, -55…125, mínimo < máximo. | 2, 6 |
| `flo`, `fhi` | Mínimo y máximo de freezer, mismas reglas. | -25, -15 |
| `aula` | Asignada y guardada por la biblioteca de AURA; el dispositivo no la fija. | Sin asignar |

Los umbrales aceptan hasta dos decimales, aunque la resolución de la sonda es
0,5 °C. Se rechazan tipos incorrectos, nombres desconocidos, claves duplicadas
del dispositivo y rangos invertidos. Los campos ausentes conservan su valor.

```json
{"command":"set_config","params":{"intervalo_s":120,"f_s":600},"command_id":"hfr-1"}
```

Usar parches y `command_id` cortos: el comando completo tiene un límite de
180 bytes. La configuración vigente se agrega mediante `describir_config`.

## Cola y cortes

La **única cola de mediciones** es la de la cátedra: hasta 400 registros en RAM
(puede reducirse si no hay memoria), respaldo en flash cuando una muestra lleva
5 minutos sin confirmación, recuperación tras reinicio y descarte de la más vieja
al llenarse. En operación normal las mediciones no escriben flash. Un reinicio
puede perder los últimos 5 minutos que todavía estaban solo en RAM; no equivale
a persistencia inmediata de todas las lecturas.

La entrada de alimentación, cuando está habilitada, reporta transiciones
con la alerta `energia`. La hoja sigue midiendo según los intervalos configurados.
El reporte de estado tiene un límite de 180 bytes. Con valores extremos
de configuración y diagnósticos como `en_flash` o `eventos_perdidos`,
puede superar el límite incluso después del recorte de la biblioteca y no
publicarse. Es una limitación del reporte. La configuración aplicada y
guardada en la placa sigue vigente.

## Compilar y cargar

1. Instalar **ESP-IDF 6.1** con [EIM](https://dl.espressif.com/dl/eim/) y abrir
   su terminal con el entorno activado. El IDE de Arduino no compila esta versión.
2. Desde la raíz del repositorio:

   ```powershell
   cd dispositivos/E1-PB-LECA-HFR01
   Copy-Item main/config_local.ejemplo.h main/config_local.h
   ```

3. Completar **solo** `MESH_ID_BYTES` y `MESH_AP_PASS` con los datos de la
   cátedra. No hace falta SSID, contraseña del WiFi, broker, UUID ni aula.
   `main/config_local.h` está ignorado por Git; no subirlo.
4. Revisar `main/hardware.h` contra el circuito real. Los componentes externos
   se descargan automáticamente con sus versiones declaradas.
5. En esa carpeta:

   ```powershell
   idf.py set-target esp32c3
   idf.py build
   ```

   Para validar una compilación completa, usar datos de mesh válidos o
   parámetros ficticios con formato válido. Los marcadores de la plantilla
   pueden generar un binario de diagnóstico que excluye la mesh.
6. Firmar el binario según la sección siguiente antes de grabarlo. Usar el
   puerto real en `idf.py -p COM5 flash monitor`. Salir con `Ctrl+]`.

La tabla es la de `Mesh3_Hoja`: `otadata`, dos aplicaciones `ota_0`/`ota_1` de
1,75 MiB y `cola` de 256 KiB, en una flash de 4 MiB. Reservar `erase-flash`
para los casos de recuperación que lo requieran, porque borra ajustes y datos
pendientes.

Si ya se compiló con los ajustes anteriores, regenerar la configuración y los
resultados locales antes de compilar: `sdkconfig` y `build/` son generados e
ignorados por Git. No borrar `main/config_local.h`. No alcanza con reemplazar
`sdkconfig.defaults` y reutilizar un `sdkconfig` sin verificación de firma.

## Actualización del firmware por la malla (OTA)

La primera instalación de esta distribución de memoria se hace **por USB**,
incluyendo bootloader y tabla de particiones. Grabar desde el principio
una imagen **firmada con RSA-3072** según [la guía §2.9](../../Mesh_Red_Completa/GUIA_NODOS_HOJA.md#29-imágenes-firmadas).
Una placa que arranca con una imagen sin firma no acepta actualizaciones OTA.

`idf.py build` produce el binario sin firmar. Para instalación, el grupo entrega
ese `.bin` y la cátedra lo firma. Para banco, seguir la guía para crear una clave
de prueba fuera del repo, firmar la primera imagen USB y usar la misma clave
para las siguientes OTA. La placa verifica la firma al actualizar por la mesh.
Esta configuración no quema eFuses.

El proyecto declara `project(aura-heladera-freezer)` y `PROJECT_VER = 1.0.2`
en `CMakeLists.txt`. La imagen es `build/aura-heladera-freezer.bin`. El tipo
describe el uso, no la ubicación ni una placa particular. Se informa como
`details.firmware.tipo` y `details.firmware.version` en AURA. Incrementar la versión para cada
nueva entrega. La actualización a la misma versión responde `sin_cambios`.

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` habilita la vuelta atrás: una versión
nueva se confirma cuando se comunica con la raíz y acumula 60 s estable. Si no
se confirma en 10 min, o reinicia antes de confirmarse, puede volver a la
anterior. La transferencia, comprobación de imagen, respaldo de la cola y
confirmación los implementa la biblioteca compartida.

Para el banco usar las herramientas oficiales y el procedimiento de
[la guía de nodos hoja](../../Mesh_Red_Completa/GUIA_NODOS_HOJA.md#28-actualización-por-la-malla-ota)
y [pruebas OTA y cortes](../../Mesh_Red_Completa/PRUEBAS_OTA_Y_CORTES.md).
La transferencia y el rollback físicos todavía no están validados en este nodo.

## Pruebas

En computadora, desde la raíz del repositorio:

```powershell
python dispositivos/E1-PB-LECA-HFR01/tests/probar.py
python -m unittest discover -s conectores/E1-PB-LECA-HFR01/tests -v
python herramientas/validar_dispositivos.py
python herramientas/validar_conectores.py
```

Las pruebas C requieren un compilador (`cc`, `gcc`, `clang` o `--cc "zig cc"`).
La primera ejecución descarga cJSON 1.7.19 oficial y comprueba SHA256. Cubren
parches inválidos, límites, atomicidad, fallos de escritura/commit NVS, CRC,
centinelas de sonda, histéresis, energía y calendario del DS3231. La entrega real
de la cola de la cátedra se verifica en banco.

En banco, con el raíz y el concentrador actualizados y el alta hecha por la cátedra:

1. Conectar la C3 por USB a la computadora y verificar arranque, hora y ambas
   sondas; si falta una, debe omitirse y aparecer su alerta. Verificar OLED y LEDs.
2. Probar intervalos distintos y umbrales distintos. Cambiar un umbral debe
   afectar al LED local aun sin red. Probar un parche inválido: no cambia nada.
   Reiniciar y comprobar que los ajustes válidos se conservaron.
3. Observar `hw/<hw_id>/data`, `status`, `response`, `ack` y `alerts/...` con las
   herramientas y credenciales del banco. Probar la configuración completa
   de status con umbrales e intervalos extremos, y con aula asignada.
4. Cortar AURA/broker más de 5 min, dejar acumular pendientes y recuperar.
   Verificar hora original, ingest_id y retiro únicamente tras ACK. Probar
   reinicio con datos ya respaldados y documentar pérdidas en RAM/overflow.
5. Con el circuito de alimentación confirmado, habilitar su GPIO y simular
   red→batería→red más de una vez. Verificar la OLED y las alertas `energia`
   de cada transición.
6. Probar el cambio de concentrador y la asignación de aula desde AURA. No
   fijar el aula en el binario. Si la alimentación externa impide asociar la
   C3, repetir con USB de computadora: la cátedra informó un problema de antena.
7. Después de la primera carga por USB, probar una actualización por la malla
   con una versión superior y comprobar `aplicado` y `details.firmware`.
   Seguir el protocolo de la cátedra para verificar el rollback; no darlo por
   probado solamente porque la imagen compila.

Quedan por confirmar cableado C3, circuito red/batería, datos privados de mesh
y alta/asignación en AURA. Quedan por medir la autonomía de batería, la vida
útil de la flash y el comportamiento físico.
