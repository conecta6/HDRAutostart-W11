# HDRAutostart

**Automatic HDR switching for Windows — activate HDR when a game or fullscreen video starts, deactivate it when it ends.**

---

## English

### The problem

Windows 11 supports HDR, but it does not switch automatically. If you leave HDR always-on, your desktop, browser, and SDR apps look washed out. If you leave it off, you have to open Display Settings every time you want to play a game or watch HDR video. HDRAutostart solves this by monitoring your system in the background and toggling HDR for you.

### Features

| Feature | Description |
|---|---|
| **Game auto-detection** | Monitors Steam / GOG / Epic (and any custom folder) for new game processes and enables HDR the moment a game launches. HDR is disabled automatically when the game closes. |
| **Browser fullscreen** | When Chrome, Edge, Firefox, Brave, Vivaldi, Opera, or other supported browsers enter fullscreen, HDR is enabled. It is disabled again as soon as the browser leaves fullscreen. To watch HDR videos, simply put the browser in fullscreen mode — usually by pressing **F11** or clicking the fullscreen button on the video player. |
| **Whitelist** | Add individual `.exe` files that should always trigger HDR, regardless of their folder location. |
| **Blacklist** | Add individual `.exe` files that should never trigger HDR (e.g. launchers inside game folders you don't want triggering HDR). |
| **Exclude** | Completely ignore specific executables or entire folders. Excluded items are never scanned — no HDR, no KTC dimming. Useful for tools, utilities, or whole directories you never want to trigger anything. |
| **KTC Local Dimming** | For KTC monitors with DDC/CI support: automatically control the Local Dimming level (VCP 0xF4) when HDR switches on or off. Separate settings for HDR and SDR modes. Off by default — safe on any monitor. |
| **KTC Sharpness** | Automatically set monitor sharpness (VCP 0x87) when HDR activates or deactivates. Configurable from 0 to 10. Default: 6. |
| **KTC Brightness** | Automatically set monitor brightness (VCP 0x10) when an SDR game launches or exits. Desktop value default: 22. SDR game value default: 100. |
| **Game profiles** | Assign per-game Local Dimming and Sharpness overrides to specific executables. When the game closes, standard global values are restored automatically. |
| **Run at startup** | One-click option to launch HDRAutostart with Windows. |
| **System tray** | Runs silently in the background. Orange icon = HDR active, grey icon = HDR inactive. Right-click for the menu. |

### Installation

1. Download `HDRAutostartSetup.exe` from [Releases](../../releases).
2. Run the installer. Windows will ask for administrator privileges — these are required to control HDR via the Windows Display API.
3. The app appears in the system tray. Right-click to configure.
4. (Optional) Enable **Run at startup** so it starts automatically with Windows.

### Configuration

All settings are stored in `hdrautostart.ini` next to the executable.

#### Tray menu reference

The tray menu currently contains these entries:

    HDRAutostart vX.Y.Z              (informational header, disabled)
    Monitored folders...
    Always enable HDR...
    Never enable HDR...
    Exclude...
    KTC Settings
      Local Dimming
        HDR (KTC)
          Off / Auto / Low / Standard / High
        SDR (KTC)
          Off / Auto / Low / Standard / High
      Sharpness (KTC)
        HDR (KTC): <current value>
        SDR (KTC): <current value>
        Desktop (KTC): <current value>
      Brightness
        Desktop (KTC): <current value>
        SDR Game (KTC): <current value>
      Game profiles...
    Run at startup                   (checked when enabled)
    GitHub
    Exit

- `Monitored folders...`, `Always enable HDR...`, `Never enable HDR...`, and `Exclude...` open the corresponding list dialogs.
- `KTC Settings -> Local Dimming` stores the selected HDR and SDR dimming mode, showing a check mark on the active value.
- `KTC Settings -> Sharpness (KTC)` shows the current HDR, SDR, and Desktop sharpness values and opens the value picker for each one.
- `KTC Settings -> Brightness` shows the current Desktop and SDR Game brightness values and opens the numeric input for each one.
- `KTC Settings -> Game profiles...` opens the per-game override manager.
- `Run at startup` toggles Windows startup registration.
- `GitHub` opens the project repository in your browser.
- `Exit` closes HDRAutostart.

#### Monitored folders

HDRAutostart comes pre-configured with your Steam library path (read from the registry). Any `.exe` that launches from inside a monitored folder will trigger HDR. You can add GOG, Epic, or any custom game folder.

> Right-click tray icon → **Monitored folders…**

#### Always enable HDR (Whitelist)

Add specific `.exe` files that should always trigger HDR no matter where they are located. Useful for games installed outside your normal library folders.

> Right-click tray icon → **Always enable HDR…**

#### Never enable HDR (Blacklist)

Add `.exe` files that should never trigger HDR. Useful for launchers (e.g. `EpicGamesLauncher.exe`) that live inside a monitored folder but are not games.

> Right-click tray icon → **Never enable HDR…**

#### Exclude

Completely ignore specific executables or entire folders — the excluded items are never processed at all (no HDR, no KTC dimming, no blacklist logic).

- **Add file** — pick a specific `.exe` to exclude.
- **Add folder** — pick a folder; all executables inside it and any subfolder are excluded recursively.

Useful for tools, benchmarks, or secondary launchers you never want HDRAutostart to react to, even if they run from inside a monitored game folder.

> Right-click tray icon → **Exclude…**

> **Note:** Common platform launchers (Steam, GOG Galaxy, Epic, Xbox, Ubisoft Connect, EA App) are always excluded automatically and do not need to be added manually.

---

### KTC Settings (KTC monitors only)

> **Note for non-KTC users:** These features exist solely because KTC is the monitor the developer uses. They are completely optional. If you ignore the KTC Settings submenu entirely, HDR auto-switching works normally on any HDR-capable monitor or TV.

All KTC-specific options are grouped under:

> Right-click tray icon → **KTC Settings**

This submenu contains Local Dimming, Sharpness, Brightness, and Game Profiles.

#### Local Dimming

HDRAutostart sends DDC/CI commands to KTC monitors to control Local Dimming automatically when HDR switches on or off. There are two independent settings — one for each mode:

**Local Dimming HDR (KTC)** — level applied when HDR is active (e.g. while playing a game):

| Setting | VCP value | Description |
|---|---|---|
| **Off** *(default)* | — | No DDC commands sent. Safe for all monitors. |
| **Auto** | 1 | Monitor controls dimming automatically |
| **Low** | 2 | Low Local Dimming |
| **Standard** | 3 | Standard Local Dimming |
| **High** | 4 | Maximum Local Dimming — recommended for HDR gaming on KTC |

**Local Dimming SDR (KTC)** — level applied when HDR is deactivated (normal desktop, SDR use):

| Setting | VCP value | Description |
|---|---|---|
| **Off** *(default)* | — | No DDC commands sent. |
| **Auto** | 1 | Monitor manages dimming automatically in SDR |
| **Low** | 2 | Low Local Dimming |
| **Standard** | 3 | Standard Local Dimming |
| **High** | 4 | Maximum Local Dimming |

Both settings are independent so you can, for example, use **High** for HDR gaming and **Auto** (or **Off**) for normal desktop use.

> KTC Settings → **Local Dimming** → HDR (KTC) / SDR (KTC)

#### Sharpness

Automatically sets the monitor's sharpness level (VCP 0x87) via DDC/CI when HDR activates or deactivates. Two independent values — one for HDR mode and one for SDR mode. Select from 0 to 10, or **Off** to send no command.

| Setting | Description |
|---|---|
| **Off** | No DDC sharpness commands sent. |
| **0 – 10** | Sharpness level sent to the monitor. Default: **6**. |

> KTC Settings → **Sharpness (KTC)** → HDR (KTC) / SDR (KTC)

#### Brightness

Automatically sets the monitor's brightness level (VCP 0x10) via DDC/CI when an SDR game (blacklisted executable) launches or exits. Two independent values:

| Setting | Default | Description |
|---|---|---|
| **Desktop (KTC)** | 22 | Brightness restored when no SDR game is running. |
| **SDR Game (KTC)** | 100 | Brightness applied when an SDR game launches. |

Both values are in the 0–100 range. Click either entry to open the numeric input dialog.

> KTC Settings → **Brightness** → Desktop (KTC) / SDR Game (KTC)

#### Game profiles

Assign per-game Local Dimming and Sharpness overrides to specific executables. When that game launches, the profile values are applied instead of the global KTC settings. When the game closes, the global standard values are automatically restored.

- **Add** — pick a `.exe` file, then choose Local Dimming and Sharpness overrides (or **Global default** to inherit the global setting).
- **Edit** — select a profile and click Edit (or double-click) to modify its values.
- **Remove** — delete the selected profile.

> KTC Settings → **Game profiles…**

---

### Supported browsers (fullscreen detection)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Windows Defender warning

Some antivirus tools (including Windows Defender) may flag `HDRAutostart.exe` as suspicious. **This is a false positive.** The app is open source — you can review every line of code in this repository. The detection is triggered because the app requests administrator privileges, reads the Windows registry, and controls system APIs, which are the exact features it needs to work. It contains no malicious code.

If you want to verify it yourself, you can [build it from source](#building-from-source) or scan the file at [VirusTotal](https://www.virustotal.com).

### Requirements

- Windows 10 version 1903 or later (HDR API requirement)
- Administrator privileges (required by the Windows HDR API)
- A monitor that supports HDR

### Building from source

Requires:
- [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) with the **Desktop development with C++** workload (MSVC x64)
- [NSIS 3.x](https://nsis.sourceforge.io/Download)

All source paths in `build.bat` are relative to `%~dp0` (the folder where the script lives). If `vcvarsall.bat` is not found, edit line 7 of `build.bat` to match your Visual Studio installation path. Then run:

```bat
build.bat
```

Output: `dist\HDRAutostart.exe` and `dist\HDRAutostartSetup.exe`

---

## Español

### El problema

Windows 11 soporta HDR, pero no lo activa automáticamente. Si dejas el HDR siempre encendido, el escritorio, el navegador y las aplicaciones SDR se ven deslavados. Si lo dejas apagado, tienes que abrir la Configuración de pantalla cada vez que quieres jugar o ver un video HDR. HDRAutostart resuelve esto monitoreando el sistema en segundo plano y cambiando el HDR por ti.

### Funcionalidades

| Función | Descripción |
|---|---|
| **Detección automática de juegos** | Monitorea Steam / GOG / Epic (y cualquier carpeta personalizada) en busca de nuevos procesos de juego y activa el HDR en cuanto el juego abre. El HDR se desactiva automáticamente cuando el juego cierra. |
| **Pantalla completa en navegador** | Cuando Chrome, Edge, Firefox, Brave, Vivaldi, Opera u otros navegadores compatibles entran en pantalla completa, se activa el HDR. Se desactiva en cuanto el navegador sale de pantalla completa. Para ver videos en HDR, simplemente pon el navegador en pantalla completa — normalmente pulsando **F11** o el botón de pantalla completa del reproductor de video. |
| **Lista blanca** | Agrega archivos `.exe` individuales que siempre deben activar el HDR, independientemente de su carpeta. |
| **Lista negra** | Agrega archivos `.exe` individuales que nunca deben activar el HDR (p. ej. launchers dentro de carpetas de juegos que no quieres que disparen el HDR). |
| **Excluir** | Ignora completamente ejecutables concretos o carpetas enteras. Los elementos excluidos no se procesan en absoluto — sin HDR, sin KTC dimming. Útil para herramientas, utilidades o directorios enteros que nunca quieres que disparen nada. |
| **Local Dimming KTC** | Para monitores KTC con soporte DDC/CI: controla automáticamente el nivel de Local Dimming (VCP 0xF4) cuando el HDR se activa o desactiva. Ajustes independientes para modo HDR y SDR. Desactivado por defecto — seguro en cualquier monitor. |
| **Nitidez KTC** | Ajusta automáticamente la nitidez del monitor (VCP 0x87) al activar o desactivar el HDR. Configurable de 0 a 10. Valor por defecto: 6. |
| **Brillo KTC** | Ajusta automáticamente el brillo del monitor (VCP 0x10) cuando se lanza o cierra un juego SDR (lista negra). Valor escritorio por defecto: 22. Valor juego SDR por defecto: 100. |
| **Perfiles de juego** | Asigna valores personalizados de Local Dimming y Nitidez a ejecutables específicos. Al cerrar el juego, se restauran automáticamente los valores globales estándar. |
| **Ejecutar al inicio** | Opción con un clic para lanzar HDRAutostart con Windows. |
| **Bandeja del sistema** | Se ejecuta silenciosamente en segundo plano. Icono naranja = HDR activo, icono gris = HDR inactivo. Clic derecho para el menú. |

### Instalación

1. Descarga `HDRAutostartSetup.exe` desde [Releases](../../releases).
2. Ejecuta el instalador. Windows pedirá privilegios de administrador — son necesarios para controlar el HDR a través de la API de Windows.
3. La app aparece en la bandeja del sistema. Clic derecho para configurar.
4. (Opcional) Activa **Ejecutar al inicio** para que arranque automáticamente con Windows.

### Configuración

Todos los ajustes se guardan en `hdrautostart.ini` junto al ejecutable.

#### Arbol del menu de bandeja

El menu de la bandeja contiene actualmente estas opciones:

    HDRAutostart vX.Y.Z              (cabecera informativa, deshabilitada)
    Carpetas monitoreadas...
    Activar HDR siempre...
    Nunca activar HDR...
    Excluir...
    Configuracion KTC
      Local Dimming
        HDR (KTC)
          Desactivado / Auto / Bajo / Estandar / Alto
        SDR (KTC)
          Desactivado / Auto / Bajo / Estandar / Alto
      Nitidez (KTC)
        HDR (KTC): <valor actual>
        SDR (KTC): <valor actual>
        Desktop (KTC): <valor actual>
      Brillo
        Desktop (KTC): <valor actual>
        SDR Game (KTC): <valor actual>
      Perfiles de juego...
    Ejecutar al inicio               (con marca cuando esta activado)
    GitHub
    Salir

- `Carpetas monitoreadas...`, `Activar HDR siempre...`, `Nunca activar HDR...` y `Excluir...` abren sus dialogos de lista correspondientes.
- `Configuracion KTC -> Local Dimming` guarda el modo de dimming seleccionado para HDR y SDR, mostrando una marca en la opcion activa.
- `Configuracion KTC -> Nitidez (KTC)` muestra los valores actuales de nitidez para HDR, SDR y Desktop, y abre el selector de valor para cada uno.
- `Configuracion KTC -> Brillo` muestra los valores actuales de brillo para escritorio y juego SDR, y abre el selector numérico para cada uno.
- `Configuracion KTC -> Perfiles de juego...` abre el gestor de perfiles por juego.
- `Ejecutar al inicio` activa o desactiva el arranque con Windows.
- `GitHub` abre el repositorio del proyecto en el navegador.
- `Salir` cierra HDRAutostart.

#### Carpetas monitoreadas

HDRAutostart viene pre-configurado con la ruta de tu biblioteca de Steam (leída del registro). Cualquier `.exe` que se lance desde dentro de una carpeta monitorizada activará el HDR. Puedes agregar carpetas de GOG, Epic o cualquier carpeta de juegos personalizada.

> Clic derecho en el icono de bandeja → **Carpetas monitoreadas…**

#### Activar HDR siempre (Lista blanca)

Agrega archivos `.exe` específicos que siempre deben activar el HDR sin importar dónde estén ubicados. Útil para juegos instalados fuera de tus carpetas de biblioteca habituales.

> Clic derecho en el icono de bandeja → **Activar HDR siempre…**

#### Nunca activar HDR (Lista negra)

Agrega archivos `.exe` que nunca deben activar el HDR. Útil para launchers (p. ej. `EpicGamesLauncher.exe`) que viven dentro de una carpeta monitorizada pero no son juegos.

> Clic derecho en el icono de bandeja → **Nunca activar HDR…**

#### Excluir

Ignora completamente ejecutables concretos o carpetas enteras — los elementos excluidos no se procesan en absoluto (sin HDR, sin KTC dimming, sin lógica de lista negra).

- **Agregar archivo** — selecciona un `.exe` concreto para excluir.
- **Agregar carpeta** — selecciona una carpeta; todos los ejecutables dentro de ella y sus subcarpetas quedan excluidos de forma recursiva.

Útil para herramientas, benchmarks o lanzadores secundarios a los que nunca quieres que HDRAutostart reaccione, aunque estén dentro de una carpeta monitorizada.

> Clic derecho en el icono de bandeja → **Excluir…**

> **Nota:** Los launchers de las plataformas principales (Steam, GOG Galaxy, Epic, Xbox, Ubisoft Connect, EA App) se excluyen automáticamente y no es necesario agregarlos.

---

### Configuración KTC (solo monitores KTC)

> **Nota para usuarios sin monitor KTC:** Estas funciones existen únicamente porque KTC es el monitor que usa el desarrollador. Son completamente opcionales. Si ignoras el submenú Configuración KTC, el cambio automático de HDR funciona con normalidad en cualquier monitor o televisor compatible con HDR.

Todas las opciones específicas de KTC están agrupadas en:

> Clic derecho en el icono de bandeja → **Configuración KTC**

Este submenú contiene Local Dimming, Nitidez, Brillo y Perfiles de juego.

#### Local Dimming

HDRAutostart envía comandos DDC/CI a los monitores KTC para controlar el Local Dimming automáticamente cada vez que el HDR se activa o desactiva. Hay dos ajustes independientes, uno para cada modo:

**Local Dimming HDR (KTC)** — nivel que se aplica cuando el HDR está activo (p. ej. mientras juegas):

| Ajuste | Valor VCP | Descripción |
|---|---|---|
| **Desactivado** *(por defecto)* | — | No se envían comandos DDC. Seguro para todos los monitores. |
| **Auto** | 1 | El monitor controla el dimming automáticamente |
| **Bajo** | 2 | Local Dimming bajo |
| **Estándar** | 3 | Local Dimming estándar |
| **Alto** | 4 | Local Dimming máximo — recomendado para jugar en HDR en KTC |

**Local Dimming SDR (KTC)** — nivel que se aplica cuando el HDR se desactiva (escritorio normal, uso en SDR):

| Ajuste | Valor VCP | Descripción |
|---|---|---|
| **Desactivado** *(por defecto)* | — | No se envían comandos DDC. |
| **Auto** | 1 | El monitor gestiona el dimming automáticamente en SDR |
| **Bajo** | 2 | Local Dimming bajo |
| **Estándar** | 3 | Local Dimming estándar |
| **Alto** | 4 | Local Dimming máximo |

Ambos ajustes son independientes, por lo que puedes usar, por ejemplo, **Alto** para jugar en HDR y **Auto** (o **Desactivado**) para el uso normal del escritorio.

> Configuración KTC → **Local Dimming** → HDR (KTC) / SDR (KTC)

#### Nitidez

Ajusta automáticamente el nivel de nitidez del monitor (VCP 0x87) vía DDC/CI al activar o desactivar el HDR. Dos valores independientes — uno para modo HDR y otro para modo SDR. Seleccionable de 0 a 10, o **Desactivado** para no enviar ningún comando.

| Ajuste | Descripción |
|---|---|
| **Desactivado** | No se envían comandos de nitidez DDC. |
| **0 – 10** | Nivel de nitidez enviado al monitor. Por defecto: **6**. |

> Configuración KTC → **Nitidez (KTC)** → HDR (KTC) / SDR (KTC)

#### Brillo

Ajusta automáticamente el nivel de brillo del monitor (VCP 0x10) vía DDC/CI cuando se lanza o cierra un juego SDR (ejecutable en lista negra). Dos valores independientes:

| Ajuste | Por defecto | Descripción |
|---|---|---|
| **Desktop (KTC)** | 22 | Brillo restaurado cuando no hay ningún juego SDR en ejecución. |
| **SDR Game (KTC)** | 100 | Brillo aplicado cuando se lanza un juego SDR. |

Ambos valores están en el rango 0–100. Haz clic en cualquiera de las entradas para abrir el selector numérico.

> Configuración KTC → **Brillo** → Desktop (KTC) / SDR Game (KTC)

#### Perfiles de juego

Asigna valores personalizados de Local Dimming y Nitidez a ejecutables específicos. Cuando ese juego se lanza, se aplican los valores del perfil en lugar de los ajustes globales de KTC. Al cerrar el juego, se restauran automáticamente los valores globales estándar.

- **Agregar** — selecciona un archivo `.exe` y elige los valores de Local Dimming y Nitidez (o **Usar valor global** para heredar el ajuste global).
- **Editar** — selecciona un perfil y haz clic en Editar (o doble clic) para modificar sus valores.
- **Eliminar** — borra el perfil seleccionado.

> Configuración KTC → **Perfiles de juego…**

---

### Navegadores compatibles (detección pantalla completa)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Aviso de Windows Defender

Algunos antivirus (incluido Windows Defender) pueden marcar `HDRAutostart.exe` como sospechoso. **Es un falso positivo.** La aplicación es de código abierto — puedes revisar cada línea de código en este repositorio. La detección se dispara porque la app solicita privilegios de administrador, lee el registro de Windows y controla APIs del sistema, que son exactamente las funciones que necesita para operar. No contiene código malicioso.

Si quieres verificarlo tú mismo, puedes [compilarlo desde el código fuente](#compilar-desde-el-código-fuente) o escanear el archivo en [VirusTotal](https://www.virustotal.com).

### Requisitos

- Windows 10 versión 1903 o posterior (requisito de la API HDR)
- Privilegios de administrador (requeridos por la API HDR de Windows)
- Un monitor compatible con HDR

### Compilar desde el código fuente

Requiere:
- [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) con la carga de trabajo **Desarrollo para escritorio con C++** (MSVC x64)
- [NSIS 3.x](https://nsis.sourceforge.io/Download)

Todas las rutas en `build.bat` son relativas a `%~dp0` (la carpeta donde vive el script). Si `vcvarsall.bat` no se encuentra, edita la línea 7 de `build.bat` para que coincida con tu ruta de instalación de Visual Studio. Luego ejecuta:

```bat
build.bat
```

Resultado: `dist\HDRAutostart.exe` y `dist\HDRAutostartSetup.exe`

---

## License

MIT
