# E1-PB-LECA-AMB01 — conector

Conector del [nodo de gas y CO2 del LabECA](../../dispositivos/E1-PB-LECA-AMB01/). Guarda lo que
manda el firmware. Las alertas de CO2 y gas las emite el firmware, no este conector.

| Qué | Dónde | Cómo |
|---|---|---|
| Guardar | `al_recibir_datos()` | `ctx.guardar_medicion(msg.values, msg.ts)`. El manifiesto declara `co2_ppm`, `temp_c`, `hum_pct` y `gas_mv` con su rango; AURA quita lo que quede fuera. Una muestra sin `gas_mv` (el MQ2 falló) es válida. |
| Alertar | — | **no alerta por umbral**: el firmware manda `alerts/dioxido_carbono_alto` y `alerts/humo_vapores` en cada cambio de estado de su aviso local (`warning` al activarse, `info` al silenciarse y al normalizarse; `details`: `estado`, `valor`, `umbral`, `saturado`). El nodo publica una muestra cada `intervalo_s` y el aviso evalúa cada 5 s: calcular el umbral acá no vería el pico. |
| Predecir | — | no predice |

Los umbrales viven solo en el firmware (`CO2_ALARMA_PPM`, `HUMO_ALARMA_MV`, en `alarma.h`): no hay copia acá que
mantener sincronizada. El de gas está **sin calibrar**: `gas_mv` es una tensión, no ppm.

Probarlo:

```bash
python3 -m unittest discover -s conectores/E1-PB-LECA-AMB01/tests
herramientas/correr_conector.py conectores/E1-PB-LECA-AMB01 --hw-id mac-<MAC de la placa> --docker aura-mosquitto
```
