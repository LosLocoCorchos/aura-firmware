# E1-PB-LECA-TAB01 — medidor de energía del tablero del LabECA

| | |
|---|---|
| **Ubicación** | Edificio 1, planta baja, LabECA — dentro del tablero eléctrico |
| **Responsable** | Grupo de IC III 2026: francobroggi, tomasvidela11, santicianflone |
| **Estado** | en desarrollo |
| **Instalado** | todavía no |
| **Transporte** | mesh ESP-WIFI-MESH (interior), en ESP-IDF 6.1, según [ADR-007](../../docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md); actualización por la malla según ADR-008 |
| **Contrato que implementa** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.3 |
| **Basado en** | [`Mesh_Red_Completa/Mesh3_Hoja`](../../Mesh_Red_Completa/Mesh3_Hoja/), sobre la biblioteca `nodo_mesh` |
| **Códigos anteriores** | ninguno |

## Qué hace

Lee por Modbus RTU (RS-485) el medidor de energía Schneider EasyLogic PM2130 del tablero del
LabECA y envía a AURA las mediciones eléctricas de la fase A: tensión, corriente, potencia
activa y cos φ. El conector está en [`conectores/E1-PB-LECA-TAB01/`](../../conectores/E1-PB-LECA-TAB01/).

## Hardware

| Componente | Modelo | Conexión |
|---|---|---|
| Placa | ESP32-C3 SuperMini, `hw_id` en la etiqueta de la placa (herramientas/leer_mac) | — |
| Transceptor RS-485 | Módulo MAX485 (DI / DE / RE / RO), DE y RE puenteados | DI ← GPIO21, RO → GPIO20, DE+RE ← GPIO5, VCC 3V3, GND |
| Medidor | Schneider EasyLogic PM2130 (METSEPM2130: RS-485, clase 0,5S, firmware v01.40) | A → D1 (22), B → D0 (23), GND → 0V (25); SHLD (24) libre. Modbus RTU, esclavo 1, 19200 baudios, 8E1 |
| Transformador de corriente | BAW CP 62/30: 100/5 A, clase 1,0, 5 VA, FS5; una pasada del conductor | al PM2130, entradas I1+ (10) e I1− (11); relación configurada 100/5 |
| Protección del circuito | Schneider Easy9 C40 (EZ9F34440), 4,5 kA; de 4 polos, aunque el laboratorio usa una sola fase | aguas arriba del TI; de ahí sale `TERMICA_A` del conector (40 A) |

## Lo que envía a AURA

Cada medición es un campo de `values` (contrato §3.1). Un valor que el medidor no entrega, que no
se pudo leer o que está fuera de su rango no aparece.

| Campo | Unidad | Rango válido | Se envía |
|---|---|---|---|
| `v_an_v` | V | 0 a 300 | siempre, cada `intervalo_s` |
| `i_a_a` | A | 0 a 120 | siempre |
| `p_w` | W | −30 000 a 30 000 | siempre |
| `cosphi_a` | — | 0 a 1 | siempre; cos φ, el factor de desplazamiento (registro 3086) |
| `thd_v_an_pct` / `thd_i_a_pct` | % | 0 a 100 / 0 a 500 | si `ENVIAR_THD` = 1 (`main/pm2130.h`) |
| `tanphi` | — | −10 a 10 | si `ENVIAR_TANFI` = 1 |
| `e_act_kwh` | kWh | 0 a 10⁹ | si `ENVIAR_ENERGIA` = 1 |

El LabECA es monofásico: las fases B y C están en el firmware (`ENVIAR_FASES_BC`), pero no se
envían. Si todo junto no entra en 160 bytes, los opcionales salen en una segunda medición.

Alertas que emite (contrato §3.5): `sensor` para cada campo obligatorio, una vez por cambio, con
motivo `sin_respuesta` (el medidor no contesta o no entrega el valor) o `fuera_de_rango`.
No acepta comandos propios.

## Configuración

Parámetros que acepta `set_config` (contrato §3.3). Se validan todos antes de aplicar alguno y se
guardan en NVS. `aula` la maneja la biblioteca. Un `intervalo_s` nuevo rige enseguida: la placa
mide en ese momento y sigue con el intervalo nuevo, sin esperar lo que quedaba del anterior.

| Parámetro | Rango | Valor en esta placa |
|---|---|---|
| `intervalo_s` | 2 a 3600 (entero) | 10 |
| `id_esclavo` | 1 a 247 (entero) | 1 |

## Compilar y probar

Tipo de firmware `aura-energia-pm2130`, versión en `PROJECT_VER` del `CMakeLists.txt` (se sube en cada
entrega). Se actualiza por la malla ([ADR-008](../../docs/adr/ADR-008-actualizacion-por-la-malla-y-tipo-de-firmware.md)).

1. ESP-IDF 6.1 (instalador EIM). Copiar `main/config_local.ejemplo.h` como `main/config_local.h` y
   completar el ID y la clave de la mesh, que da la cátedra (no se sube al repositorio).
2. Desde esta carpeta: `idf.py set-target esp32c3` y `idf.py build`. La compilación no firma.
3. Firmar aparte con la clave del banco o de la instalación (GUIA_NODOS_HOJA.md, 2.9) y grabar la imagen
   firmada: `idf.py -p <puerto> flash monitor`. La primera vez, por USB y con la flash borrada, porque
   la tabla de particiones tiene dos lugares para el firmware.
4. Al arrancar, el monitor muestra el tipo y la versión. Para la prueba de OTA, `MOSTRAR_LED_AL_MEDIR`
   en `main.c` enciende el LED azul en cada medición: la 1.0.0 sin LED y la 1.0.1 con LED.
5. Tests de la lógica del medidor (trama, validación, conversión, rangos), sin placa: `make -C tests`.

## Notas

- **Prueba de la actualización por la malla por HTTP (2026-10-30):** los 8 casos que se podían
  hacer en el banco salieron bien, sin perder mediciones. El informe, con los extractos de los
  registros que respaldan cada caso, está en
  [`pruebas/2026-10-30-ota-http/`](pruebas/2026-10-30-ota-http/README.md).
- **Prueba de la actualización por la malla por HTTPS (2026-11-06):** el gateway rechazó los
  certificados de otra CA y para otra IP, y con el correcto la hoja se actualizó instalada en el
  tablero, sin cables; también se la pasó a la 1.1.0 de esta carpeta y se comprobó en placa el cambio
  de intervalo. Informe en [`pruebas/2026-11-06-ota-https/`](pruebas/2026-11-06-ota-https/README.md).
- Al armar el banco, el PM2130 tenía la relación del TI en el valor de fábrica (5/5), lo que
  daba corrientes, potencias y energías 20 veces menores. Se configuró 100/5 y quedó guardado.
- Se detectó la polaridad del TI invertida (potencia activa negativa). Se corrige intercambiando
  S1 y S2 con el circuito **sin tensión**: nunca abrir el secundario de un TI con corriente.
- En el módulo MAX485, A y B estaban rotulados al revés respecto del PM2130: si no hay respuesta
  por Modbus, lo primero es intercambiarlos.
- La consola va por el USB del chip (`sdkconfig.defaults`), así la UART0 no usa los pines 20 y 21
  del RS-485.
- **Precisión con poca carga.** Con la carga típica del aula (1 a 2 A), el TI de 100 A trabaja al
  1 o 2 % de su corriente nominal. Su norma (IEC 60044-1, en la etiqueta como IEC 44-1) fija el
  error de un TI de clase 1 recién desde el 5 % de la nominal (±3 % ahí, ±1 % desde el 100 %). Con
  esa carga, la corriente, la potencia y el cos φ medidos pueden tener más error que el de la clase.
  Además, como la protección es de 40 A, el TI de 100 A nunca pasa del 40 % de su nominal: uno de
  menor relación (por ejemplo, 50/5) aprovecharía mejor la escala.
- **El cos φ no es el factor de potencia verdadero.** `cosphi_a` es el registro 3086 del PM2130: el
  factor de desplazamiento, calculado con la onda fundamental, sin armónicos. El factor de potencia
  verdadero (P / S) también ve los armónicos. En el tablero, el cos φ da alrededor de 0,98, pero
  P / (V · I) da 0,77 (mediana de las 444 mediciones de las pruebas de OTA): con las cargas
  electrónicas del laboratorio, la corriente tiene mucha distorsión armónica. El PM2130 mide los
  dos; para confirmar la distorsión se puede encender `ENVIAR_THD` y mirar `thd_i_a_pct`.
