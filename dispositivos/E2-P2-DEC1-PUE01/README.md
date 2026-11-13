# E2-P2-DEC1-PUE01 — puerta de Decanato 1

Control de acceso con llavero de proximidad de la puerta de Decanato 1.

**Modelo:** [`_modelos/PUE-RFID`](../_modelos/PUE-RFID/)

| | |
|---|---|
| **Ubicación** | Edificio 2, segundo piso, Decanato 1 |
| **Responsable** | losHijosDelStackOverFlow (IC IV 2026) |
| **Estado** | en desarrollo: el firmware del modelo compila, sin probar en placa ni instalado |
| **Instalado** | todavía no |
| **Transporte** | ESP-WIFI-MESH, según [ADR-006](../../docs/adr/ADR-006-esp-wifi-mesh-reemplaza-a-esp-now-en-interior.md) y [ADR-007](../../docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md) |
| **Contrato que implementa** | [`CONTRATO_MQTT.md`](../../docs/CONTRATO_MQTT.md) v4.x, con los comandos del [ADR-009](../../docs/adr/ADR-009-puertas-con-llavero-la-puerta-decide-y-aura-supervisa.md) |
| **Basado en** | [`_modelos/PUE-RFID`](../_modelos/PUE-RFID/) |
| **Códigos anteriores** | ninguno |

El firmware, el hardware, lo que envía, el `set_config` y los comandos están en la ficha del
[modelo](../_modelos/PUE-RFID/README.md). Acá va solo lo propio de esta puerta.

## Lo propio de esta instalación

| | |
|---|---|
| **`hw_id` de la placa** | a completar al instalarla (`mac-…`) |
| **Aula en la mesh** | a definir con la cátedra (topología de E2-P2 sin decidir) |
| **Sensor de la puerta** | `reed` (por defecto); a confirmar al instalar |
| **`pulso_ms` / `aviso_s`** | por defecto (5000 ms / 15 s); se ajusta con `set_config` y se anota acá |
| **Llaveros habilitados** | los administra AURA (`tarjeta_agregar` / `tarjeta_quitar`) |
| **Dónde va la placa** | a decidir: cuanto más difícil de alcanzar, más tiempo tiene para avisar |
| **Tipo de cerradura** | Luber 500, fail-secure (trabada sin corriente), a confirmar con la etiqueta (8–12 V, ¿alterna?). Ver los pendientes del modelo |
