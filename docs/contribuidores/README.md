# Contribuidores

Un archivo por persona: `docs/contribuidores/<tu-usuario-de-github>.md`. Es tu **primer PR** al
repositorio, antes que el de tu dispositivo.

## Para qué

GitHub no corre el CI de un PR que viene de un fork si su autor todavía no tiene ningún commit
en este repositorio: hasta entonces, cada corrida espera a que la cátedra la apruebe a mano. Con
este PR mergeado, el CI de tus PRs siguientes corre solo, en cada push. Y de paso practicás el
circuito completo (fork, rama, commit, PR, revisión) con algo que no se puede romper.

Lo tiene que hacer **cada integrante del grupo que vaya a pushear**, no uno por grupo: GitHub lo
cuenta por persona.

## Cómo

1. Hacé un **fork** de este repositorio (si ya tenés uno, traé `main` primero).
2. Creá una rama, por ejemplo `contribuidor/<tu-usuario>`.
3. Agregá `docs/contribuidores/<tu-usuario>.md` con este contenido, completo:

   ```markdown
   # <tu-usuario-de-github>

   - **Nombre:** Nombre Apellido
   - **Materia:** Ingeniería en Computación III (o IV), 2026
   - **Grupo / dispositivo:** E1-PB-LECA-XXX01 (o "todavía sin asignar")
   ```

4. Commiteá solo ese archivo y abrí el PR contra `main`. El nombre del archivo es tu usuario
   exacto de GitHub, en minúsculas si así lo escribís al loguearte.

No pongas mail, teléfono ni DNI: el repositorio es **público**.
