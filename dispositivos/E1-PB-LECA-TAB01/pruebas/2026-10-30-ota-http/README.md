# Prueba en placa: actualización por la malla por HTTP con E1-PB-LECA-TAB01 (2026-10-30)

Grupo de IC III 2026 (@francobroggi, @tomasvidela11, @santicianflone).

Es la primera prueba de la actualización por la malla (OTA) hecha por un grupo con su propio
dispositivo: la hoja del medidor de energía del tablero del LabECA (PM2130 por RS-485, ESP32-C3
SuperMini). Se probó el código del PR #36 (`seguridad/etapa-1`, commit `54b66ec`, hoy en `main`):
imágenes firmadas, actualización aceptada solo desde la raíz y vuelta atrás automática. El firmware
de la hoja fue la versión 1.0.0 de esta carpeta. Después de la prueba, con la revisión del PR #11,
la carpeta pasó a la 1.1.0; esos cambios no tocan la OTA (ver la ficha).

**Resultado: ✅ los 8 casos que se podían hacer en este banco salieron como se esperaba.** La
descarga por HTTPS, la otra mitad de lo que pidió la cátedra, queda para la ronda 2 (ver
[Lo que sigue](#lo-que-sigue)).

- La hoja pasó de 1.0.0 a 1.0.1 y después a 1.0.2 por la malla, sin cables. Cada versión nueva se
  confirmó sola después de 60 s de andar.
- Rechazó todo lo que tenía que rechazar: una imagen de otro tipo (la frenó el gateway antes de
  transferirla), una sin firma y una firmada con otra clave (las rechazó la hoja al verificar la
  firma).
- Ante un corte de energía en plena transferencia, el gateway volvió a empezar solo. Ante una
  versión que no podía entrar a la malla, la hoja volvió sola a la anterior a los 10 minutos.
- **No se perdió ni se duplicó ninguna medición:** las 234 que tomó la hoja llegaron a AURA una
  sola vez, incluidas 58 que guardó en flash durante la vuelta atrás.

## Cómo se probó

### El banco

| Placa | Hardware | Firmware |
|---|---|---|
| Gateway | Waveshare ESP32-S3 (módulo N32R16V), por USB a la notebook | `aura-gateway` 1.0.0, compilado para el banco: `DESCUBRIR_BROKER 1`, `OTA_PERMITIR_HTTP 1` y una CA propia en `OTA_CA_PEM` (para la ronda 2) |
| Concentrador del aula 1 | Waveshare ESP32-S3 (módulo N16R8), por USB a la notebook | `aura-concentrador` 1.0.0, autorizado desde el tablero (`autorizar_concentrador`) |
| Concentrador del aula 2 | Igual, alimentado con un cargador | Igual |
| Hoja | ESP32-C3 SuperMini con un MAX485, conectada al PM2130 (esclavo 1, 19200, 8E1) | `aura-energia-pm2130` 1.0.0 |

- **Red:** un router WiFi de 2,4 GHz aislado, en el canal 6 fijo y sin internet.
- **Una sola notebook con Windows** hizo todo: ESP-IDF 6.1, `broker.py` (con el tablero), `ota.py`
  y los monitores de la hoja y del gateway.
- **La malla:** 4 nodos. La hoja, en el aula 1, llega al gateway por su concentrador: 2 saltos.

### La firma

Cada grupo usa en el banco su propia clave de prueba (RSA-3072), guardada fuera del repo. Se
compila sin firmar y se firma aparte (`GUIA_NODOS_HOJA.md`, 2.9). Para eso usamos un script
propio, `firmar.py`, que firma, verifica la firma y **se niega a firmar dos veces la misma imagen**:
`espsecure sign-data` lo permite sin avisar, y la imagen queda deformada. Como sirve para cualquier
hoja, lo proponemos aparte, junto con las otras herramientas del banco. Cada
placa se grabó una sola vez por USB, con su 1.0.0 firmada; después de eso, todo llegó por la malla.

### Las imágenes de prueba

| Imagen | Qué cambia | Firma | Caso |
|---|---|---|---|
| `aura-energia-pm2130` 1.0.1 | Enciende el LED azul de la placa en cada medición (`MOSTRAR_LED_AL_MEDIR 1`) | Clave de prueba | P1, P5 |
| `aura-energia-pm2130` 1.0.2 | Igual que la 1.0.1 | Sin firma | P6 |
| `aura-energia-pm2130` 1.0.2 | Igual | Otra clave | P7 |
| `aura-energia-pm2130` 1.0.2 | Igual | Clave de prueba | P10 |
| `aura-energia-pm2130` 1.0.3 | El último byte del `MESH_ID` cambiado: no puede entrar a la malla | Clave de prueba | P11 |
| `aura-hoja-ejemplo` 1.0.0 | La hoja de ejemplo de la cátedra, sin cambios | Clave de prueba | P4 |

Para saber a simple vista qué versión corre, cada una lo informa al arrancar
(`Firmware aura-energia-pm2130, version 1.0.1 (LED al medir: si)`). AURA lo recibe en
`details.firmware`.

## Resultados

Los tiempos salen de `ota.py` y del registro del broker. Cada caso tiene su extracto de los
registros más abajo, en [Extractos](#extractos-de-los-registros).

| # | Prueba | Resultado |
|---|---|---|
| P1 | Hoja 1.0.0 → 1.0.1 (`--hw`) | ✅ **`aplicado` a 1 min 44 s del pedido:** la descarga al gateway tardó 9 s, los 1,18 MB por la malla 23 s (2 saltos), y `aplicado` llegó 72 s después de `recibido` (reinicio más 60 s de estabilidad). La hoja verificó la firma y volvió con 1.0.1 y el LED activado. |
| P5 | La misma versión otra vez | ✅ `aplicado` con `sin_cambios` en 10 s, sin transferir nada: el gateway sabe la versión de cada placa por su `details.firmware`. |
| P4 | Una imagen de otro tipo (`aura-hoja-ejemplo`) | ✅ `rechazado` `tipo_distinto` en 7 s. La frenó el gateway antes de transferir nada. |
| P6 | Una imagen sin firma | ✅ `rechazado` `imagen_invalida`. La hoja la recibió entera (16 s por la malla) y la rechazó al verificar la firma. Siguió en 1.0.1. |
| P7 | Una imagen firmada con otra clave | ✅ Igual que P6. |
| P10 | Corte de energía de la hoja en plena transferencia (al 38 %) | ✅ La hoja arrancó con su 1.0.1. El gateway lo notó, volvió a ofrecer la imagen 17 s después del último avance y la transfirió entera en 30 s. **`aplicado` con 1.0.2, a 2 min 23 s del pedido.** |
| P11 | Una versión que no puede entrar a la malla (1.0.3, otro `MESH_ID`) | ✅ La hoja la instaló, no encontró padre, y **a los 600 s volvió sola a 1.0.2**. Mientras tanto, el gateway la marcó `offline` (`sin_tramas`), y contestó `rechazado` `volvio_a_la_version_anterior` a los 10 min 4 s de `recibido`. Antes de reiniciar, la hoja respaldó su cola en flash: la 1.0.2 recuperó **58 mediciones** y las mandó en unos 40 s. |
| P12 | Los datos, durante todas las pruebas | ✅ **234 de 234 mediciones** llegaron a AURA, cada una persistida una sola vez y ninguna perdida: los `ingest_id` confirmados en la hoja y los recibidos por el broker coinciden uno a uno. Una se reenvió (la primera después de la vuelta atrás), y AURA contestó `duplicado` por su `ingest_id`. El gateway terminó con 0 lotes rechazados y 0 tramas inválidas. |

Además, durante el armado:

- **La autorización de concentradores** del PR #36 funcionó: `autorizar_concentrador` →
  `aplicado` en los dos. Después, el gateway aceptó 353 lotes y no rechazó ninguno.
- **La validación de la configuración** también: un `set_config` con el aula vacía, mandado por
  error desde el tablero, terminó en `rechazado` `aula_invalida`, y la hoja siguió como estaba.
- **Las mediciones**, durante toda la prueba: 230,0 a 233,5 V; 1,03 a 1,23 A; 187 a 219 W; cos φ
  de 0,96 a 0,99.

## Lo que observamos

1. **La firma la verifica solo la hoja, después de recibir la imagen entera.** En P6 y P7, la
   imagen ocupó la malla 16 s antes del rechazo. El gateway verifica el SHA-256 y el tipo antes de
   ofrecer una imagen (por eso P4 no transfirió nada), pero no la firma. Lo sugerimos y la cátedra
   lo anotó como pendiente de la etapa 2 en el ADR-008. No es un problema de seguridad: la hoja la
   rechaza igual.
2. **Avisos `[WND-RX] … timeout` de la malla** durante cada transferencia. Ninguna falló por eso.
3. **`--- WARNING: Checksum mismatch between flashed and built applications`** en el monitor,
   después de cada OTA. Es esperable: la hoja corre una versión distinta de la última que se
   compiló en la PC. No indica un problema.
4. **Sin internet en el banco, el gateway no tiene hora** (`hora_valida: false`), y las mediciones
   viajan sin hora. Las 58 recuperadas en P11 llegaron, pero no pudimos comprobar que conserven su
   hora original; la cátedra sí lo comprobó el 22/10 (prueba 8c de
   [`PRUEBAS_OTA_Y_CORTES.md`](../../../../Mesh_Red_Completa/PRUEBAS_OTA_Y_CORTES.md)).

## Límites de esta prueba

- **Solo HTTP.** La ronda por HTTPS (la CA propia del gateway y `ota.py --https`) queda para la
  semana próxima, con tres casos: P13, el certificado correcto; P14, uno de otra CA; P15, uno para
  otra IP. Los tres certificados los comprobamos en una PC, con un cliente que los verifica contra
  la CA como el gateway, pero falta hacerlo en placa.
- **Una sola notebook** hizo de servidor de imágenes, de broker y de consola. Eso no cambia lo que
  se probó de la malla, pero la descarga fue siempre desde la red local, no desde el servidor de
  AURA.
- **Una sola hoja de este tipo:** no se probaron varias placas a la vez (P2) ni tipos distintos a
  la vez (P3). Hacen falta hojas de otros grupos.
- **Dos casos no se podían probar en este banco:** P8 (HTTP prohibido), porque el gateway de banco
  acepta HTTP a propósito, y P9 (imagen alterada), porque `ota.py` calcula el SHA-256 por su
  cuenta.
- **Cada caso se corrió una vez,** con las placas a menos de un metro entre sí.
- **Firmware de banco:** la clave de firma es la del grupo, y el gateway busca el broker por la red
  y acepta HTTP. Nada de esto va a la instalación.

## Para otros grupos: lo que nos frenó

- **Compilar para el S3:** hay que elegir ese chip al instalar ESP-IDF con EIM. Si falta, aparece
  `xtensa-esp32s3-elf-gcc … was not found in the PATH`; se resuelve reinstalando la 6.1 con
  esp32c3 y esp32s3.
- **La terminal:** usen el acceso directo `IDF_v6.1_Powershell` que deja EIM. Si `idf.py` termina
  en un `SyntaxError` de Node.js, esa terminal no tiene ESP-IDF activado.
- **`internal compiler error: Segmentation fault` en `esp_lcd_panel_rgb.c`:** es un error conocido
  del compilador en Windows (issue #12180 de ESP-IDF), que aparece de vez en cuando. Se resuelve
  repitiendo `idf.py build`.
- **`credenciales.h` del gateway:** editen las líneas que ya existen, no agreguen otras arriba. Un
  `#define` repetido no da error: gana el último.
- **Firmar:** `espsecure sign-data` firma dos veces la misma imagen sin avisar; `firmar.py` lo
  evita. Y `espsecure` no crea carpetas: la de destino tiene que existir.
- **Puertos:** las Waveshare aparecen como dos puertos COM. Cualquiera sirve para grabar, pero hay
  que usar siempre el mismo.

## Lo que sigue

1. **Ronda 2, por HTTPS:** P14, P15 y P13, en ese orden. Las placas se vuelven a grabar desde cero,
   porque las del laboratorio se comparten.
2. **P2 y P3,** si hay hojas de otros grupos en el banco.
3. **Etapa 2 (eFuses),** recién con la OTA probada: en una SuperMini de descarte, cifrado de flash
   en modo desarrollo, y repetir P1 y P10 con la placa cifrada.

## Extractos de los registros

Son textuales, salvo las MACs, las IPs y las rutas de la PC, que quedan como `<hoja>`, `<gateway>`,
`<concentrador-1>`, `<concentrador-2>` y `<pc>`. Las líneas de `ota.py` y del broker tienen la
hora del reloj; las de los monitores, el tiempo desde que arrancó cada placa, en milisegundos. Los
registros completos (unas 5.700 líneas) quedan fuera del repo; el grupo los conserva y los puede
compartir con la cátedra.

### P1: 1.0.0 → 1.0.1

`ota.py`:

```
20:16:49  Pido la actualización a 1 placa(s): <hoja>
20:16:58  <hoja>: transmitido - el gateway le ofreció la imagen
20:17:21  <hoja>: recibido - tiene la imagen verificada y se reinicia
20:18:33  <hoja>: aplicado - volvió con aura-energia-pm2130 1.0.1
```

Monitor de la hoja:

```
I (623196) esp_image: Verifying image signature...
I (623256) secure_boot_v2_rsa: Signature verified successfully!
W (623476) AURA_OTA: Imagen aura-energia-pm2130 1.0.1 verificada: me reinicio con ella
I (262) TAB01: Firmware aura-energia-pm2130, version 1.0.1 (LED al medir: si)
I (60282) AURA_FW: Versión 1.0.1 confirmada: ya no vuelve a la anterior
```

### P5: la misma versión

```
20:22:21  <hoja>: aplicado - ya tenía aura-energia-pm2130 1.0.1
```

### P4: otro tipo

`ota.py` y monitor del gateway:

```
20:20:37  Ojo: <hoja> es aura-energia-pm2130 y la imagen es aura-hoja-ejemplo: el gateway la va a rechazar (un cambio de uso se hace por USB)
20:20:44  <hoja>: rechazado - rechazada: tipo_distinto (tiene aura-energia-pm2130 1.0.1)

W (1491457) OTA_GW: <hoja> es aura-energia-pm2130 y la imagen es aura-hoja-ejemplo
```

### P6 y P7: sin firma y con otra clave

Monitor de la hoja, en P6 (P7 da lo mismo):

```
I (369922) esp_image: Verifying image signature...
E (369982) secure_boot_v2_rsa: Signature verification failed, err: -149
E (369982) esp_image: Secure boot signature verification failed
W (370072) esp_image: image valid, signature bad
E (370072) esp_ota_ops: New image failed verification
W (370072) AURA_OTA: Imagen inválida: ESP_ERR_OTA_VALIDATE_FAILED
```

`ota.py`, en P6 y en P7:

```
20:23:32  <hoja>: rechazado - rechazada: imagen_invalida
20:24:37  <hoja>: rechazado - rechazada: imagen_invalida
```

### P10: corte de energía

`ota.py`:

```
20:27:49  gateway: <hoja> 38 % (en cola 0)
20:28:06  <hoja>: transmitido - el gateway le ofreció la imagen
20:28:37  <hoja>: recibido - tiene la imagen verificada y se reinicia
20:29:47  <hoja>: aplicado - volvió con aura-energia-pm2130 1.0.2
```

Monitor del gateway:

```
W (1931397) OTA_GW: <hoja>: sesion_desconocida, vuelvo a empezar (1 de 2)
I (1963997) OTA_GW: <hoja> tiene la imagen verificada (30 s): espero que vuelva con 1.0.2
```

### P11: versión que no entra a la malla

Monitor de la hoja:

```
I (262) TAB01: Firmware aura-energia-pm2130, version 1.0.3 (LED al medir: si)
W (29332) NODO_MESH: No veo ningún padre posible (¿gateway encendido? ¿mismo MESH_ID?), se reintenta
I (300282) COLA: 1 mediciones sin confirmar hace 300 s: respaldadas en flash (en flash: 1)
E (600262) AURA_FW: La versión nueva no se confirmó en 600 s: vuelvo a la anterior
I (600282) COLA: 29 mediciones sin confirmar respaldadas en flash antes de reiniciar (en flash: 58)
I (600502) esp_ota_ops: Rollback to previously worked partition.
I (245) TAB01: Firmware aura-energia-pm2130, version 1.0.2 (LED al medir: si)
I (295) COLA: Respaldo en flash: 1024 lugares, 58 mediciones recuperadas
```

Broker y `ota.py`:

```
20:32:09  [<gateway>]  hw/<hoja>/status  {"status": "offline", "details": {"motivo": "sin_tramas"}}
20:41:47  <hoja>: rechazado - rechazada: volvio_a_la_version_anterior (tiene aura-energia-pm2130 1.0.2)
```

### P12: los datos

El único reenvío de toda la prueba, y cómo lo contestó AURA:

```
20:41:55  [aura-falsa]  hw/<hoja>/ack  {"ingest_id": "6a79bccf-3589-487e-9ecf-3df6c387ae22", "resultado": "duplicado"}
```

El conteo (234 mediciones, cada `ingest_id` confirmado una vez) sale de cruzar el monitor de la
hoja con el registro del broker. Las 8 mediciones que la hoja tomó al final, después de que se
apagó el broker, quedaron en su cola; no son parte de la prueba.
