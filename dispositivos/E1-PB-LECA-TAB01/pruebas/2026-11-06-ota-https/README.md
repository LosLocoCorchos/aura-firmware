# Prueba en placa: actualización por la malla por HTTPS con E1-PB-LECA-TAB01 (2026-11-06)

Grupo de IC III 2026 (@francobroggi, @tomasvidela11, @santicianflone).

Es la segunda ronda de la prueba de OTA, la que quedó pendiente en la
[ronda 1](../2026-10-30-ota-http/README.md): la descarga de la imagen por **HTTPS**, con el gateway
verificando el certificado del servidor contra una CA propia. Es la primera prueba en placa de
ese camino. Se usó el código del PR #36 (`seguridad/etapa-1`, hoy en `main`).

Esta vez, además, la hoja estaba **instalada en el tablero del LabECA**, con la fuente de su caja y
sin cable a la PC, y compartía la malla con hojas de otros tres grupos.

**Resultado: ✅ los tres casos salieron como se esperaba,** y se agregaron dos pruebas más:

- El gateway **rechazó** los dos certificados que tenía que rechazar (uno de otra CA y uno para
  otra IP), sin descargar nada y en un segundo.
- Con el certificado correcto, la hoja pasó de 1.0.0 a 1.0.4 en 1 min 28 s, **instalada en el
  tablero y sin cables**.
- Después se la actualizó, también por HTTPS, a la **1.1.0**, el firmware corregido de esta carpeta.
  La hoja quedó corriendo la versión que está en el repo.
- Con la 1.1.0 instalada se comprobó en placa la corrección del intervalo que pidió la revisión del
  PR #11: un `intervalo_s` nuevo rige **en el mismo segundo**.
- **Ninguna medición perdida ni duplicada:** 209 de 209 llegaron a AURA una sola vez, todas con su
  hora, incluso durante las dos actualizaciones.

## Cómo se probó

### Qué cambió respecto de la ronda 1

| | Ronda 1 (2026-10-30) | Ronda 2 (2026-11-06) |
|---|---|---|
| Router | Uno propio, aislado y sin internet | El del aula (`AURA-BANCO`), con internet |
| Hora | Sin hora (`hora_valida: false`) | Con hora de internet: todas las mediciones llevan `ts` |
| La hoja | Por USB a la notebook, en la mesa | Instalada en el tablero, con la fuente de su caja y sin cable |
| La malla | 4 nodos: gateway, 2 concentradores y nuestra hoja | Hasta 6 nodos: se sumaron tres hojas de otros grupos (una cerradura con tarjeta, una puerta con solenoide y una de ambiente) |
| Descarga de la imagen | HTTP | HTTPS, con la CA del banco en `OTA_CA_PEM` |

El gateway, los concentradores y las imágenes de prueba fueron los mismos de la ronda 1, salvo un
cambio: el gateway se recompiló con la clave del WiFi del aula. Se grabaron de nuevo desde cero,
porque las placas del laboratorio se comparten.

### Los certificados

La notebook sirvió la imagen con `ota.py --https`. Se armaron tres certificados para su IP:

| Certificado | Firmado por | Para la IP | Caso |
|---|---|---|---|
| `pc_otra.pem` | Otra CA, que el gateway no conoce | La de la notebook | P14: tiene que fallar |
| `pc_otra_ip.pem` | La CA del banco | Otra IP (`10.9.9.9`) | P15: tiene que fallar |
| `pc.pem` | La CA del banco | La de la notebook | P13: tiene que funcionar |

Antes de la ronda, en una PC y con un cliente que verifica contra la CA igual que el gateway,
comprobamos que el primero falla por la CA, el segundo por la IP, y el tercero se acepta.

## Resultados

| # | Prueba | Resultado |
|---|---|---|
| P14 | Certificado de otra CA | ✅ `rechazado` `descarga_fallida`, **un segundo después del pedido.** El gateway cortó en el *handshake* TLS (`-0x2700`: falló la verificación del certificado) y no descargó nada. A la hoja no le llegó nada. |
| P15 | Certificado de la CA correcta, pero para otra IP | ✅ Igual que P14: `-0x2700` y `rechazado` `descarga_fallida` en menos de un segundo. |
| P13 | Certificado correcto | ✅ **`aplicado` con 1.0.4 a 1 min 28 s del pedido:** descarga por HTTPS en 10 s, transferencia por la malla en 8 s, y `aplicado` 70 s después de `recibido` (reinicio más 60 s de estabilidad). |
| Extra | La hoja instalada, de 1.0.4 a la 1.1.0 del repo, por HTTPS | ✅ `aplicado` a 1 min 30 s del pedido, con 11 s por la malla. |
| Extra | El intervalo nuevo rige enseguida (revisión del PR #11) | ✅ Al subirlo a 120 s, la hoja midió en el mismo segundo. Al bajarlo a 10 s, 45 s después, **también midió en el mismo segundo**, en lugar de esperar los 75 s que le faltaban, y siguió cada 10 s. |
| Datos | Durante toda la ronda | ✅ **209 de 209 mediciones** llegaron a AURA, cada una persistida una sola vez y todas con su hora. Durante las transferencias la hoja siguió midiendo y mandando, y en los dos reinicios no se perdió ninguna. El gateway terminó con 0 lotes rechazados y 0 tramas inválidas. |

Las mediciones del tablero durante la ronda: 230 a 237 V, 1,1 a 2,1 A, 188 a 299 W, cos φ de 0,86
a 0,96.

## Lo que observamos

1. **El gateway no distingue por qué rechazó un certificado.** P14 y P15 dieron el mismo error
   (`-0x2700`) y el mismo motivo para AURA (`descarga_fallida`), que es también el que daría una red
   caída. **Sugerencia para la cátedra:** que el gateway muestre en el registro el motivo de la
   verificación (los *flags* de mbedTLS: CA desconocida, nombre o IP que no coincide, vencido) y
   que conteste un motivo propio, por ejemplo `certificado_invalido`. Así, quien opera AURA sabe si
   tiene que revisar la red o el certificado del servidor.
2. **Una hoja viva cuyo sensor falla termina viéndose como caída.** Con el medidor desconectado (a
   propósito, durante el armado), la hoja mandó bien sus cuatro alertas de sensor
   (`sin_respuesta`). Pero como no tenía mediciones válidas, dejó de mandar telemetría, y a los 84 s
   el gateway la marcó `offline` (`sin_tramas`), aunque seguía conectada a la malla.
   Para AURA, "el sensor falla" y "la placa se cayó" terminan pareciéndose. **Sugerencia:** que la
   hoja mande un reporte de estado cuando no tiene mediciones, para que el gateway la siga viendo
   viva. Se puede resolver en la biblioteca o en el firmware de cada dispositivo; lo dejamos a
   criterio de la cátedra.
3. **La transferencia por la malla fue de dos a tres veces más rápida que en la ronda 1:** de 8 a 11 s,
   contra 22 a 30 s, para una imagen del mismo tamaño, por el mismo camino de 2 saltos, y con señal
   igual o peor (de -60 a -86 dBm). No sabemos por qué: el ancho de canal no lo explica, porque en las
   dos rondas la malla trabajó a 40 MHz. Con una sola medición de cada lado, lo dejamos como dato.
4. **Las mediciones atrasadas conservan su hora.** Con hora de internet, la única medición que llegó
   tarde (14 s, al volver la hoja al tablero) llegó con la hora en que se tomó. Responde en parte un
   límite de la ronda 1, aunque con una sola muestra.

## Límites de esta prueba

- **Cada caso se corrió una vez.**
- **El gateway de banco acepta también HTTP** (`OTA_PERMITIR_HTTP 1`). No se probó el gateway de
  instalación, que acepta solo HTTPS, ni la descarga desde el servidor de AURA con un certificado
  público.
- **Sin el monitor de la hoja durante las OTA,** porque estaba instalada sin cable. La evidencia sale
  de `ota.py`, del monitor del gateway y del registro del broker; las líneas internas de la hoja,
  como la verificación de la firma, no se ven. La firma ya se había comprobado en la ronda 1 (P6 y
  P7).
- **Hubo hojas de otros grupos en la malla, pero no se actualizaron:** varias placas a la vez (P2) y
  tipos distintos a la vez (P3) siguen pendientes.
- **La CA y los certificados son de banco,** para una IP de la red local.

## Para otros grupos: lo que nos frenó

- **Mosquitto instalado como servicio de Windows.** Si en la PC hay un Mosquitto (por ejemplo, de
  otra materia), escucha en `127.0.0.1:1883` y Windows le da prioridad sobre `broker.py`. Entonces
  `ota.py`, que por defecto se conecta a `127.0.0.1`, le manda el pedido a Mosquitto, que no se lo
  pasa a nadie, y se queda esperando sin error. El gateway no se entera porque se conecta por la IP
  de la red. Se detecta con
  `Get-NetTCPConnection -LocalPort 1883 -State Listen | Select-Object LocalAddress, OwningProcess`.
  Se resuelve agregando `--broker <IP de la PC>` a `ota.py`, o apagando el servicio
  (`Stop-Service mosquitto`, como administrador).
- **Con el router del aula:** el gateway tiene que compilarse con la clave de esa red, y la IP de la
  PC cambia, así que hay que rehacer el certificado de la PC para la IP nueva. La CA no cambia, así
  que el gateway no se recompila por eso. Si la red es compartida, conviene comprobar que no haya
  otro `broker.py` en la red antes de conectar el gateway, porque se queda con el primero que
  contesta. Para comprobarlo usamos un script que hace la misma pregunta que el gateway y muestra
  quién contesta; como sirve para cualquier banco, lo proponemos aparte, junto con las otras
  herramientas.
- **Para mandar a una placa un comando que el tablero no tiene** (uno propio del dispositivo), la
  entrada del tablero acepta cualquiera, con PowerShell:
  `Invoke-RestMethod -Method Post -Uri 'http://localhost:8080/cmd?hw=<hw_id>&command=<comando>&params=<JSON en formato URL>'`.
- **Para guardar los registros:** en el monitor, `Ctrl+T` y después `Ctrl+L` (el monitor avisa con
  una línea `---` y escribe un `log.….txt` en la carpeta del proyecto). Con `ota.py`, agregar
  `| Tee-Object -FilePath <archivo>`, y correrlo con `python -u`: si no, la salida se ve recién al
  final.

## Lo que sigue

1. **P2 y P3,** con hojas de otros grupos en el banco: cada grupo tiene que traer su imagen nueva
   firmada con su propia clave.
2. **Etapa 2 (eFuses),** ya con la OTA probada por HTTP y por HTTPS: en una SuperMini de descarte,
   cifrado de flash en modo desarrollo, y repetir P1 y P10 con la placa cifrada.
3. **`TERMICA_A` del conector,** con la corriente de la protección del circuito del LabECA.

## Extractos de los registros

Son textuales, salvo las MACs y las IPs, que quedan como `<hoja>`, `<gateway>` y `<pc>`. Las líneas
de `ota.py` y del broker tienen la hora del reloj; las del monitor del gateway, el tiempo desde que
arrancó, en milisegundos. Los registros completos quedan fuera del repo; el grupo los conserva y los
puede compartir con la cátedra.

### P14: certificado de otra CA

`ota.py`:

```
19:21:21  Servida en https://<pc>:8070/aura-energia-pm2130-1.0.4.bin
19:21:23  Pido la actualización a 1 placa(s): <hoja>
19:21:24  <hoja>: rechazado - rechazada: descarga_fallida
```

Monitor del gateway:

```
I (8108522) OTA_GW: Descargando https://<pc>:8070/aura-energia-pm2130-1.0.4.bin
E (8108552) esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x2700
E (8108552) esp-tls: Failed to open new connection
W (8108562) OTA_GW: <hoja>: rechazada (descarga_fallida)
```

### P15: certificado para otra IP

```
19:22:21  Pido la actualización a 1 placa(s): <hoja>
19:22:21  <hoja>: rechazado - rechazada: descarga_fallida

E (8165622) esp-tls-mbedtls: mbedtls_ssl_handshake returned -0x2700
W (8165632) OTA_GW: <hoja>: rechazada (descarga_fallida)
```

### P13: certificado correcto

`ota.py`:

```
19:22:31  Pido la actualización a 1 placa(s): <hoja>
19:22:31  HTTP: <gateway> descarga la imagen (1183744 bytes)
19:22:40  <hoja>: transmitido - el gateway le ofreció la imagen
19:22:49  <hoja>: recibido - tiene la imagen verificada y se reinicia
19:23:59  <hoja>: aplicado - volvió con aura-energia-pm2130 1.0.4
```

Monitor del gateway, con una medición de la hoja que llega en plena transferencia:

```
I (8185292) OTA_GW: Imagen descargada y verificada: aura-energia-pm2130 1.0.4, 1183744 bytes
I (8188252) OTA_GW: <hoja>: 25 %
I (8188392) GATEWAY: MQTT <- hw/<hoja>/ack {"ingest_id": "be67bb14-…", "resultado": "persistido"}
I (8193792) OTA_GW: <hoja> tiene la imagen verificada (8 s): espero que vuelva con 1.0.4
I (8263392) OTA_GW: <hoja>: aura-energia-pm2130 1.0.4 anda hace 60 s: aplicada
```

### Extra: la 1.1.0 del repo

```
19:27:13  Pido la actualización a 1 placa(s): <hoja>
19:27:34  <hoja>: recibido - tiene la imagen verificada y se reinicia
19:28:43  <hoja>: aplicado - volvió con aura-energia-pm2130 1.1.0
```

### Extra: el intervalo

Broker. La hora de la izquierda es la de llegada, y `ts` es cuándo midió la hoja, en UTC (tres horas
más que la hora local):

```
19:32:44  hw/<hoja>/response  {"status": "aplicado", … "config": {"aula": "1", "intervalo_s": 120, …}}
19:32:44  hw/<hoja>/data      {…, "ts": "2026-11-06T22:32:44Z", …}
19:33:29  hw/<hoja>/response  {"status": "aplicado", … "config": {"aula": "1", "intervalo_s": 10, …}}
19:33:29  hw/<hoja>/data      {…, "ts": "2026-11-06T22:33:29Z", …}
19:33:40  hw/<hoja>/data      {…, "ts": "2026-11-06T22:33:39Z", …}
```

Con el firmware anterior, la medición de las 19:33:29 habría llegado recién a las 19:34:44.

### La hoja sin medidor (observación 2)

```
18:23:16  hw/<hoja>/alerts/sensor  {"severity": "high", "message": "Sonda v_an_v con falla", "details": {"campo": "v_an_v", "motivo": "sin_respuesta"}, …}
18:24:40  hw/<hoja>/status         {"status": "offline", "details": {"motivo": "sin_tramas"}}
18:52:50  hw/<hoja>/status         {"status": "online", …}
```
