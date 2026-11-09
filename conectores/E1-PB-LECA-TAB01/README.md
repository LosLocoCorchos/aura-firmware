# E1-PB-LECA-TAB01 — conector del medidor de energía del LabECA

Procesa lo que manda el medidor Schneider PM2130 del tablero del LabECA (leído por Modbus RTU
desde una ESP32-C3 SuperMini, hoja de la mesh): guarda las mediciones eléctricas de la fase A y
alerta cuando la instalación sale de lo normal.

## Campos

Tienen que coincidir con lo que manda el firmware (`values`, contrato §3.1).

| Campo | Unidad | Rango | |
|---|---|---|---|
| `v_an_v` | V | 0 a 300 | obligatorio |
| `i_a_a` | A | 0 a 120 | obligatorio (TI 100/5 A) |
| `p_w` | W | −30 000 a 30 000 | obligatorio |
| `cosphi_a` | — | 0 a 1 | obligatorio; cos φ, el factor de desplazamiento (ver abajo) |
| `tanphi` | — | −10 a 10 | opcional; + inductivo, − capacitivo |
| `thd_v_an_pct` | % | 0 a 100 | opcional |
| `thd_i_a_pct` | % | 0 a 500 | opcional |
| `e_act_kwh` | kWh | 0 a 10⁹ | opcional |

Las fases B y C están en el manifiesto, comentadas: el aula es monofásica y el firmware no las
envía.

`cosphi_a` es el cos φ: el factor de desplazamiento que entrega el PM2130 (registro 3086),
calculado con la onda fundamental, sin armónicos. No es el factor de potencia verdadero (P / S):
con las cargas del laboratorio, el cos φ ronda 0,98 y P / (V · I) ronda 0,77, por la distorsión
armónica de la corriente.

## Alertas

Una por cruce del umbral y otra (`info`) al volver a lo normal. Para saber cómo estaba, compara
con la última medición de la última hora en la que esa regla se podía evaluar (`ctx.serie`).

| Tipo | Severidad | Cuándo |
|---|---|---|
| `subtension` | warning | tensión por debajo de 198 V (220 V − 10 %) |
| `sobretension` | warning | tensión por encima de 242 V (220 V + 10 %) |
| `sobrecorriente` | high | corriente por encima del 80 % de la protección del circuito |
| `potencia_negativa` | warning | potencia activa por debajo de −50 W: en un aula, TI invertido |
| `bajo_cosphi` | warning | cos φ por debajo de 0,85 con al menos 200 W de carga. Con menos carga no se evalúa: ni se avisa ni se da por normalizado |

Los umbrales de tensión y de cos φ son una **propuesta**, y están arriba de todo en `conector.py`.
La protección del circuito es una llave Schneider Easy9 C40: `TERMICA_A` es 40 A, y la alerta de
sobrecorriente salta desde los 32 A.

La alerta `bajo_cosphi` mira el cos φ, que es lo que corrige un banco de capacitores; un factor de
potencia bajo por armónicos no se corrige así. Con el umbral de 0,85 no salta con el tablero
funcionando normal (lo verifica el test con datos reales).

Un campo que falta en una muestra no activa ni normaliza ninguna alerta: no se sabe. Un valor
fuera de rango tampoco: AURA lo quita antes. Y una muestra así tampoco hace que se repita una
alerta que seguía activa.

## Probarlo

```bash
python3 -m unittest discover -s conectores/E1-PB-LECA-TAB01/tests
herramientas/correr_conector.py conectores/E1-PB-LECA-TAB01 --hw-id mac-<MAC de la placa> --docker aura-mosquitto
herramientas/correr_conector.py conectores/E1-PB-LECA-TAB01 --archivo conectores/E1-PB-LECA-TAB01/tests/tablero_real.jsonl
```

Los tests incluyen 148 mediciones reales del tablero (`tests/tablero_real.jsonl`, de las pruebas de
OTA del 30/10 y del 6/11 de 2026): con el tablero funcionando normal, el conector las guarda todas y
no emite ninguna alerta. Si un cambio de umbral o de regla hace saltar alertas con esos datos, el
test lo avisa. La última línea de arriba pasa esos mismos datos por el conector como lo haría AURA.
