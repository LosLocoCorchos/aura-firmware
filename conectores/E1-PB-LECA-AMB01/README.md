# E1-PB-LECA-AMB01 — conector

Conector del [nodo de gas y CO2 del LabECA](../../dispositivos/E1-PB-LECA-AMB01/). Guarda lo que
manda el firmware y alerta cuando el CO2 o el gas pasan su umbral.

| Qué | Dónde | Cómo |
|---|---|---|
| Guardar | `al_recibir_datos()` | `ctx.guardar_medicion(msg.values, msg.ts)`. El manifiesto declara `co2_ppm`, `temp_c`, `hum_pct` y `gas_mv` con su rango; AURA quita lo que quede fuera. Una muestra sin `gas_mv` (el MQ2 falló) es válida. |
| Alertar | `al_recibir_datos()` | `dioxido_carbono_alto` (`warning`, desde 1500 ppm) y `gas_alto` (`high`, desde 2400 mV), **una vez por cruce**, más una alerta `info` al volver. Compara con la última medición guardada (`ctx.serie`). |
| Predecir | — | no predice |

Los umbrales son los de la alarma local del firmware (`CO2_ALARMA_PPM`, `GAS_ALARMA_MV`, en `alarma.h`). El de gas está
**sin calibrar**: `gas_mv` es una tensión, no ppm. Si se ajusta en el firmware, ajustarlo también en
`conector.py`.

Probarlo:

```bash
python3 -m unittest discover -s conectores/E1-PB-LECA-AMB01/tests
herramientas/correr_conector.py conectores/E1-PB-LECA-AMB01 --hw-id mac-<MAC de la placa> --docker aura-mosquitto
```
