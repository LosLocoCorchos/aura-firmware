# Dispositivos

**Una carpeta por dispositivo, con todo adentro**: su firmware (`<CÓDIGO>.ino`), su ficha
(`README.md`: dónde está, qué hardware usa, qué envía, cómo se configura), sus bibliotecas y sus
tests. El código de la carpeta dice **dónde está y qué es**, de un vistazo.

Para empezar uno nuevo, copiá
[`../ejemplos/plantilla_dispositivo/`](../ejemplos/plantilla_dispositivo/) (LoRaWAN) o
[`../Mesh_Red_Completa/Mesh3_Hoja/`](../Mesh_Red_Completa/Mesh3_Hoja/) (mesh).

## Varios dispositivos con el mismo firmware

Cuando se instalan varias placas iguales (por ejemplo, las nueve puertas del Edificio 2), el
firmware se escribe **una sola vez**, en un **modelo**: `_modelos/<MODELO>/`. El modelo tiene el
código, las bibliotecas, los tests y una ficha técnica (hardware, qué envía, `set_config`,
comandos). `<MODELO>` es el tipo seguido de una variante: `PUE-RFID`.

Cada dispositivo instalado conserva su carpeta con su código, pero **solo con la ficha**
(`README.md`): ubicación, responsable, estado, lo propio de esa instalación y una línea que
apunta al modelo:

```
**Modelo:** [`_modelos/PUE-RFID`](../_modelos/PUE-RFID/)
```

La carpeta del dispositivo no lleva firmware: un arreglo se hace en el modelo y llega a todas
las placas. La identidad no va en el código: cada placa se presenta con su `hw_id` y AURA la
asigna al dispositivo ([ADR-005](../docs/adr/ADR-005-identidad-por-placa-y-mapeo-en-aura.md)).
Lo que cambia de una instalación a otra (un tiempo, si el sensor es normal abierto o cerrado)
se configura por `set_config` y se anota en la ficha del dispositivo, no en `#define`.

En un proyecto ESP-IDF dentro de `_modelos/`, la ruta a los componentes tiene un nivel más:
`${CMAKE_CURRENT_LIST_DIR}/../../../Mesh_Red_Completa/componentes`.

Con dos dispositivos iguales alcanza con copiar la carpeta y anotar *Basado en* en la ficha.
Desde el tercero, o si se sabe de antemano que van a ser varios, va un modelo.

## Formato del código

```
EDIFICIO-PISO-RECINTO-TIPOnn          ejemplo: E1-PB-LECA-HFR01
```

Mayúsculas, separado por guiones. `nn` numera los dispositivos del mismo tipo en el mismo
recinto, desde `01`. Cada parte sale de las tablas de abajo: **si necesitás una abreviatura
nueva, agregala a su tabla en el mismo PR** que crea el dispositivo. El CI rechaza códigos que
no respeten el formato o que usen abreviaturas que no están acá.

**Si un equipo se muda**, su carpeta se renombra con el código nuevo y la ficha anota el
anterior en *Códigos anteriores*. En AURA conserva su `device_id`, así que el historial de
mediciones no se corta.

## Mapa

Las abreviaturas siguen el modelo de ubicaciones de AURA (edificio → piso → recinto).

### Edificios

| Código | Edificio | Pisos |
|---|---|---|
| `E1` | Edificio 1 | `PB`, `P1`, `AZ` |
| `E2` | Edificio 2 | `PB`, `P1`, `P2`, `AZ` |

### Pisos

El número en AURA es el orden del piso dentro de su edificio, así que la azotea no tiene el
mismo número en todos: va arriba del último piso.

| Código | Piso | Número en AURA |
|---|---|---|
| `PB` | Planta baja | 0 |
| `P1` | Primer piso | 1 |
| `P2` | Segundo piso (solo E2) | 2 |
| `AZ` | Azotea | E1: 2 · E2: 3 |

### Recintos

| Código | Recinto | Edificio y piso |
|---|---|---|
| `LECA` | LabECA | E1, planta baja |
| `TERR` | Terraza (al aire libre) | E1, azotea |
| `RECT` | Rectorado | E2, segundo piso |
| `VICE` | Vicerrectorado | E2, segundo piso |
| `DEC1` | Decanato 1 | E2, segundo piso |
| `DEC2` | Decanato 2 | E2, segundo piso |
| `CSUP` | Consejo Superior | E2, segundo piso |
| `ACA1` | Secretaría Académica, oficina 1 | E2, segundo piso |
| `ACA2` | Secretaría Académica, oficina 2 | E2, segundo piso |
| `ACA3` | Secretaría Académica, oficina 3 | E2, segundo piso |
| `SINV` | Secretaría de Investigación y Transferencia Tecnológica | E2, segundo piso |

Los recintos se nombran por oficina, no por persona: si alguien cambia de oficina, el código
no cambia.

### Tipos

El tipo dice **sobre qué actúa** el dispositivo: la magnitud que mide en el ambiente, el equipo
que monitorea, o lo que acciona. Una placa que mide varias magnitudes del ambiente a la vez es
`AMB`, y su ficha lista qué mide.

| Código | Qué es | Clase |
|---|---|---|
| `TEM` | Temperatura del ambiente | magnitud |
| `HUM` | Humedad del ambiente | magnitud |
| `LUZ` | Intensidad de luz | magnitud |
| `CDC` | Dióxido de carbono (CO2) del ambiente | magnitud |
| `AMB` | Varias magnitudes del ambiente en una placa | magnitud |
| `HFR` | Heladera con freezer | equipo |
| `HEL` | Heladera | equipo |
| `FRZ` | Freezer | equipo |
| `INC` | Incubadora | equipo |
| `HOR` | Horno | equipo |
| `TAB` | Tablero eléctrico (consumo) | equipo |
| `TAN` | Tanque de agua (nivel) | equipo |
| `CRT` | Cortina | actuador |
| `PUE` | Puerta (cerradura y sensor de apertura) | actuador |
| `GWM` | Raíz de la mesh (gateway hacia AURA) | infraestructura |
| `SAL` | Relevo de la mesh (antes, nodo de sala) | infraestructura |

## Dispositivos

| Código | Qué es | Responsable | Estado |
|---|---|---|---|
| [`E1-AZ-TERR-TAN01`](E1-AZ-TERR-TAN01/) | Nivel de dos tanques de agua de la terraza (LoRaWAN) | losHijosDelStackOverFlow (IC IV 2026) | instalado |
| [`E1-PB-LECA-HFR01`](E1-PB-LECA-HFR01/) | Heladera-freezer del LabECA | grupo de IC IV 2026 | en desarrollo |
| [`E1-PB-LECA-CDC01`](E1-PB-LECA-CDC01/) | CO2 del LabECA (mesh ESP-WIFI-MESH) | grupo de IC IV 2026 | en desarrollo |
| [`E1-PB-LECA-TAB01`](E1-PB-LECA-TAB01/) | Medidor de energía del tablero del LabECA | grupo de IC III 2026 | en desarrollo |