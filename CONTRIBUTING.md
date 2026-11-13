# Cómo sumar un dispositivo

Guía para los grupos de Ingeniería en Computación III y IV.

## Una carpeta por dispositivo

Cada dispositivo instalado tiene **una carpeta con todo adentro** en [`dispositivos/`](dispositivos/):
su firmware, su ficha, sus bibliotecas y sus tests. La carpeta se llama con el **código del
dispositivo**, que dice dónde está y qué es:

```
E1-PB-LECA-HFR01        Edificio 1, planta baja, LabECA, heladera-freezer 01
```

Cada parte sale del mapa de [`dispositivos/README.md`](dispositivos/README.md). Ejemplo: en el
LabECA hay un sensor de temperatura (`E1-PB-LECA-TEM01`), uno de humedad (`E1-PB-LECA-HUM01`),
el consumo del tablero (`E1-PB-LECA-TAB01`), uno de luz (`E1-PB-LECA-LUZ01`) y el actuador de una
cortina (`E1-PB-LECA-CRT01`): cinco dispositivos, cinco carpetas.

Si un dispositivo nuevo usa el mismo firmware que otro (por ejemplo, otra heladera con el mismo
hardware), se copia esa carpeta con el código nuevo, y la ficha dice de cuál está basado. Si van
a ser **más de dos placas iguales** (las puertas de un edificio), el firmware va una sola vez en
`dispositivos/_modelos/<MODELO>/` y cada dispositivo queda con su carpeta y solo la ficha, que
apunta al modelo. Ver [*Varios dispositivos con el mismo firmware*](dispositivos/README.md#varios-dispositivos-con-el-mismo-firmware).

## 1. Antes de escribir código

1. **Leé el contrato**: [`docs/CONTRATO_MQTT.md`](docs/CONTRATO_MQTT.md). Como mínimo §2.4 (un
   dispositivo es una placa), §3.1 (qué va en `values`), §3.3 (`set_config`) y §3.5 (alertas).
2. **Definí el transporte** con la cátedra: mesh ESP-WIFI-MESH si está en interior, LoRaWAN si
   está en exterior o lejos ([ADR-003](docs/adr/ADR-003-dos-transportes-mesh-interior-lorawan-exterior.md),
   [ADR-006](docs/adr/ADR-006-esp-wifi-mesh-reemplaza-a-esp-now-en-interior.md)).
3. **Armá el código del dispositivo** con el mapa. Si te falta una abreviatura (un recinto
   nuevo, un tipo nuevo), la agregás a su tabla en el mismo PR.
4. **Pedí el alta del dispositivo** en AURA. Le pasás a la cátedra el `hw_id` de tu placa: si
   es mesh, el que imprime `herramientas/leer_mac` (`mac-…`); si es LoRaWAN, el DevEUI. La
   cátedra asigna esa placa a tu dispositivo en AURA, y te da los datos de la mesh del edificio
   (ID y clave) o las credenciales OTAA. Nada de eso se commitea. Mientras la placa no esté
   asignada, lo que mande queda en **cuarentena** en AURA y se recupera al asignarla.

## 2. Tu carpeta

0. **Primer PR, una sola vez por persona:** tu archivo en
   [`docs/contribuidores/`](docs/contribuidores/). Hasta que tengas un commit mergeado, GitHub
   no corre el CI de tus PRs sin que la cátedra lo apruebe a mano.
1. Hacé un **fork** de este repositorio y trabajá en una rama.
2. **Mesh (interior):** copiá [`Mesh_Red_Completa/Mesh3_Hoja/`](Mesh_Red_Completa/Mesh3_Hoja/) a
   `dispositivos/<CÓDIGO>/` (ver abajo). **LoRaWAN (exterior):** copiá
   [`ejemplos/plantilla_dispositivo/`](ejemplos/plantilla_dispositivo/) a `dispositivos/<CÓDIGO>/`
   y renombrá el `.ino` igual que la carpeta (`E1-AZ-TERR-TAN01.ino`): Arduino lo exige. En los
   dos casos te llevás también la ficha (`README.md`) de la plantilla.
3. Completá el `README.md`, que es la ficha: ubicación, responsable, hardware, campos de
   `values`, alertas, parámetros de `set_config` y su valor en esta placa.
4. Listá en `bibliotecas.txt` cada biblioteca con su versión (`OneWire@2.3.8`). El CI instala
   exactamente esas.
5. Sumá el dispositivo a la tabla *Dispositivos* de `dispositivos/README.md`, y tu carpeta a
   `.github/CODEOWNERS` con los usuarios de GitHub del grupo. Si trabajás sobre un modelo, va
   también `dispositivos/_modelos/<MODELO>/`.

**Si el dispositivo va por la mesh**, se programa en **ESP-IDF 6.1**, no en Arduino
([ADR-007](docs/adr/ADR-007-mesh-en-esp-idf-con-concentrador-por-aula.md), desde el 2026-10-20).
Se instala con el instalador de Espressif, [EIM](https://dl.espressif.com/dl/eim/). La placa es
la **ESP32-C3 SuperMini** y la pone el proyecto.

1. Copiá `Mesh_Red_Completa/Mesh3_Hoja/` como `dispositivos/<CÓDIGO>/`. En su `CMakeLists.txt`,
   cambiá la ruta de los componentes a
   `set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../Mesh_Red_Completa/componentes")`
   y el nombre del proyecto (`project(...)`) por tu código en minúsculas.
2. Copiá `main/config_local.ejemplo.h` como `main/config_local.h` y completá el ID y la clave de
   la mesh, que te da la cátedra. **No hace falta el WiFi del edificio**: solo el raíz se
   conecta al router.
3. En `main/main.c` escribís lo propio del dispositivo: la medición, `aplicar_config()` (valida
   **todo** y recién después aplica y guarda), `describir_config()` (incluye `intervalo_s`) y,
   si acciona algo, `ejecutar_comando()`. Lo demás lo hace la biblioteca
   [`nodo_mesh`](Mesh_Red_Completa/componentes/nodo_mesh/): la unión a la mesh, el aula, la cola,
   el `ingest_id`, los reintentos hasta que AURA confirma y la hora.
4. `idf.py set-target esp32c3` y `idf.py -p <puerto> build flash monitor`.

La guía completa (API, reglas, checklist, problemas frecuentes) es
[`Mesh_Red_Completa/GUIA_NODOS_HOJA.md`](Mesh_Red_Completa/GUIA_NODOS_HOJA.md). El aula no va en
el firmware: la asigna AURA con `set_config {"aula": …}`; hasta entonces la hoja manda directo al
raíz y sus datos llegan igual. Para probar sin AURA, la cátedra levanta el banco
([`Mesh_Red_Completa/Broker_MQTT/`](Mesh_Red_Completa/Broker_MQTT/)).

> La versión Arduino de la mesh (`comun/nodo_mesh.h`, `ejemplos/sensor_ejemplo/`,
> `ejemplos/actuador_ejemplo/`) queda **obsoleta**: no se usa para dispositivos nuevos, y los que
> ya están escritos así se portan. El CI todavía no compila los proyectos ESP-IDF: compilalos
> vos antes de abrir el PR.

Si usás código de `comun/`, incluilo con ruta relativa (`#include "../../comun/protocolo_aura.h"`).
Si tu lógica se puede probar sin placa (formato de trama, validación de rangos, cola), poné los
tests en `dispositivos/<CÓDIGO>/tests/` con un `Makefile`: el CI los corre solo.

## 3. Tu conector

El **conector** es el código que procesa, dentro de AURA, lo que manda tu dispositivo: lo
valida, lo guarda, alerta y, si querés, predice. Va en `conectores/<CÓDIGO>/`, con el mismo
código que tu dispositivo (ver [`conectores/README.md`](conectores/README.md)).

1. Copiá [`ejemplos/plantilla_conector/`](ejemplos/plantilla_conector/) a `conectores/<CÓDIGO>/`.
2. En `conector.toml`: `nombre` igual a la carpeta, tus usuarios de GitHub, y **un bloque
   `[campos.<nombre>]` por cada campo que manda tu firmware** (y por cada uno que calcule tu
   conector), con su unidad y su rango físico. Lo que no esté declarado, o esté fuera de rango,
   AURA no lo guarda. Cada campo dice además cómo se muestra en la web:
   - `etiqueta`: cómo se lo nombra ("Nivel tanque 1"), hasta 40 caracteres. Si falta, el nombre del campo.
   - `rol`: `principal` va en la tarjeta del Dashboard y en el gráfico de la ficha; `auxiliar`,
     solo en la ficha, aparte (la medición cruda, un valor derivado de otro); `oculto` se guarda
     pero no se muestra. Si falta, `principal`. Pocos principales: los que alguien mira para
     saber cómo está el dispositivo.
   - Los campos se muestran en el orden del manifiesto.
3. En `conector.py`, `al_recibir_datos(self, msg, ctx)`: con `msg.values` y `msg.ts` decidís
   `ctx.guardar_medicion(...)` o `ctx.descartar("motivo")`. Si querés predecir, `predecir(self, ctx)`
   y `[prediccion]` en el manifiesto.
4. Tests en `tests/`, con `ContextoDePrueba`: corren sin AURA ni placa.
   ```bash
   python3 -m unittest discover -s conectores/<CÓDIGO>/tests
   ```
5. En el banco, contra lo que manda tu placa:
   ```bash
   herramientas/correr_conector.py conectores/<CÓDIGO> --hw-id mac-<MAC de tu placa> --docker aura-mosquitto
   ```
6. **Probalo como corre en AURA.** AURA no importa tu conector en su proceso: lo corre en un
   proceso aparte, sin credenciales, con 256 MB de memoria, 5 s de CPU, 64 archivos abiertos y
   10 s por muestra. Con `--subproceso`, `correr_conector.py` hace lo mismo:
   ```bash
   herramientas/correr_conector.py conectores/<CÓDIGO> --archivo mensajes.jsonl --subproceso
   ```
   Si algo anda sin `--subproceso` y falla con él (un import pesado, una muestra que tarda), en
   AURA va a fallar igual. Lo que imprimas con `print()` va al log, no a la salida.

Lo que puede usar un conector es solo el `Contexto`: `guardar_medicion`, `descartar`, `serie`,
`guardar_prediccion`, `alertar`, `config_vigente` y `log`. No tiene acceso a la base, a la red ni
a archivos, y AURA puede recrearlo en cualquier momento: lo que tenga que recordar lo lee con
`ctx.serie()`. Si lanza una excepción, se guarda la muestra cruda (los campos dentro de rango) con una alerta
`conector_fallo`, y el dispositivo no reenvía. Python 3.11 o posterior, sin bibliotecas externas por ahora.

## 4. Lo que no se negocia

- **Ninguna credencial en el repo.** WiFi, clave de la mesh, AppKey, tokens, MAC de producción: todo en
  `config_local.h`, que está en el `.gitignore`. Este repo es **público**.
- **Ningún dato falso.** Si un sensor no responde, ese campo no se envía. No se reemplaza por
  otra lectura (por ejemplo, la temperatura interna del ESP32) ni por un valor fijo.
- **Configuración remota validada.** Cada parámetro de `set_config` se valida contra un rango
  antes de aplicarse, y se guarda en memoria no volátil. Un comando mal armado no puede dejar
  el nodo inutilizable.
- **No tocar `comun/` ni `infraestructura/` en el mismo PR que tu dispositivo.** Si necesitás
  un cambio ahí, abrí un issue o un PR aparte: afecta a todos los dispositivos.

## 5. Si el equipo se muda

Su carpeta se renombra con el código nuevo (también el `.ino`), y la ficha anota el anterior en
*Códigos anteriores*. En AURA conserva su `device_id`, así que el historial no se corta.

## 6. El pull request

Abrilo contra `main` de este repo. La plantilla del PR trae un checklist. El CI:

| Chequeo | Qué hace |
|---|---|
| Sin secretos | Rechaza `config_local.h` versionados y corre `gitleaks` sobre toda la historia |
| Códigos de dispositivos | Cada carpeta respeta el formato y el mapa, tiene su ficha y su `.ino` se llama igual |
| Tests de host | `comun/tests`, `infraestructura/*/tests`, `herramientas/tests` y los `tests/` de cada dispositivo |
| Conectores | Tests de `aura_sdk`, manifiesto y clase de cada conector, y sus `tests/` |
| Compilar sketches | Compila cada sketch para ESP32-C3 SuperMini (o para el FQBN de su `fqbn.txt`) |

Con todo en verde, la cátedra revisa y mergea.
