# E1-PB-LECA-CDC01 — conector

Conector del dispositivo [`E1-PB-LECA-CDC01`](../../dispositivos/E1-PB-LECA-CDC01/): CO2 del
laboratorio con un MH-Z19C, por la mesh ESP-WIFI-MESH (`hw_id` `mac-907069abbfe4`).

| Qué | Dónde | Cómo |
|---|---|---|
| Guardar | `al_recibir_datos()` | `co2_ppm` (lo único que manda la placa), con el rango de `conector.toml` (400 a 5000 ppm); lo que esté fuera de rango no se guarda |
| Alertar | `al_recibir_datos()` | un solo tipo, `aire_calidad`: `warning` al pasar de 1000 ppm, `high` al pasar de 1500 ppm e `info` al volver a verde (al bajar de rojo a amarillo avisa `warning`). **Una vez por cambio de nivel**, comparando con la última medición guardada (`ctx.serie`) |
| Umbrales | `umbrales()` | los de los LEDs de la placa; si `set_config` los cambia (`atencion_ppm`, `alto_ppm`), usa los que reportó la placa |

Probarlo:

```bash
python3 -m unittest discover -s conectores/E1-PB-LECA-CDC01/tests
herramientas/correr_conector.py conectores/E1-PB-LECA-CDC01 --hw-id mac-907069abbfe4 --docker aura-mosquitto
```

El segundo necesita la mesh y el gateway publicando en el broker de AURA.
