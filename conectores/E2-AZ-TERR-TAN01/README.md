# Conector de E2-AZ-TERR-TAN01 — nivel de 3 tanques de agua

Conector del dispositivo [`E2-AZ-TERR-TAN01`](../../dispositivos/E2-AZ-TERR-TAN01/): tres sensores
ultrasónicos AJ-SR04M que miden la distancia al agua de tres tanques de la terraza del Edificio 2.
Va por LoRaWAN (`hw_id` `eui-<DevEUI>`).

| Qué | Cómo |
|---|---|
| Guardar | `distancia_tanqueN_cm` (cm con un decimal, de 25 a 600) y `nivel_tanqueN_pct` (0 a 100 %, lo calcula el nodo con la altura y el lleno del tanque). Cada mensaje trae los tres tanques (salvo uno cuyo sensor no midió). Lo que está fuera de rango lo quita AURA. `25` cm significa "25 cm o menos": el agua en la zona ciega del sensor, tanque lleno |
| Alertar | `tanque_bajo`: `warning` cuando el nivel baja de `NIVEL_MINIMO_PCT` (20 %) e `info` cuando vuelve a superar el mínimo + `HISTERESIS_PCT` (25 %), una vez por cruce y por tanque. |
| Alertar | `sensor`: `warning` cuando un tanque falta en `MENSAJES_SIN_DATOS` (3) mensajes seguidos que traen a los otros (el nodo no manda un tanque cuyo sensor no midió) e `info` cuando vuelve. No depende del intervalo. Si fallan los tres a la vez no hay aviso: el mensaje de estado del nodo no llega a AURA (pendiente 19 del contrato; ver la [ficha](../../dispositivos/E2-AZ-TERR-TAN01/README.md)) |
| Predecir | todavía no |

El dispositivo solo mide: la bomba la maneja un control externo, y `tanque_bajo` avisa si no llenó
a tiempo. La altura y el lleno de cada tanque se configuran en el nodo (por `set_config` desde AURA), no acá: ver la
[ficha](../../dispositivos/E2-AZ-TERR-TAN01/README.md#comandos).

Probarlo, desde la raíz del repo:

```bash
python -m unittest discover -s conectores/E2-AZ-TERR-TAN01/tests
python herramientas/correr_conector.py conectores/E2-AZ-TERR-TAN01 --archivo mensajes.jsonl
```
