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
| **Browser fullscreen** | When Chrome, Edge, Firefox, Brave, Vivaldi, Opera, or other supported browsers enter fullscreen (F11 / fullscreen video), HDR is enabled. It is disabled again as soon as the browser leaves fullscreen. |
| **Whitelist** | Add individual `.exe` files that should always trigger HDR, regardless of their folder location. |
| **Blacklist** | Add individual `.exe` files that should never trigger HDR (e.g. launchers inside game folders you don't want triggering HDR). |
| **KTC Local Dimming** | For KTC monitors with DDC/CI support: automatically control the Local Dimming level via the monitor's internal VCP command when HDR activates. Off by default — safe to use on any monitor. |
| **Run at startup** | One-click option to launch HDRAutostart with Windows. |
| **System tray** | Runs silently in the background. Orange icon = HDR active, grey icon = HDR inactive. Right-click for the menu. |

### Installation

1. Download `HDRAutostart.exe` from [Releases](../../releases).
2. Run it. Windows will ask for administrator privileges — these are required to control HDR via the Windows Display API.
3. The app appears in the system tray. Right-click to configure.
4. (Optional) Enable **Run at startup** so it starts automatically with Windows.

### Configuration

All settings are stored in `hdrautostart.ini` next to the executable.

#### Monitored folders

HDRAutostart comes pre-configured with your Steam library path (read from the registry). Any `.exe` that launches from inside a monitored folder will trigger HDR. You can add GOG, Epic, or any custom game folder.

> Right-click tray icon → **Monitored folders…**

#### Always enable HDR (Whitelist)

Add specific `.exe` files that should always trigger HDR no matter where they are located. Useful for games installed outside your normal library folders.

> Right-click tray icon → **Always enable HDR…**

#### Never enable HDR (Blacklist)

Add `.exe` files that should never trigger HDR. Useful for launchers (e.g. `EpicGamesLauncher.exe`) that live inside a monitored folder but are not games.

> Right-click tray icon → **Never enable HDR…**

#### KTC Local Dimming (KTC monitors only)

HDRAutostart sends DDC/CI commands to KTC monitors to control Local Dimming automatically when HDR switches on or off. There are two independent settings — one for each mode:

**Local Dimming HDR (KTC)** — level applied when HDR is active (e.g. while playing a game):

| Setting | VCP value | Description |
|---|---|---|
| **Off** *(default)* | — | No DDC commands sent. Safe for all monitors. |
| **Auto** | 1 | Monitor controls dimming automatically |
| **Low** | 2 | Low Local Dimming |
| **Standard** | 3 | Standard Local Dimming |
| **High** | 4 | Maximum Local Dimming — recommended for HDR gaming on KTC |

> Right-click tray icon → **Local Dimming HDR (KTC)**

**Local Dimming SDR (KTC)** — level applied when HDR is deactivated (normal desktop, SDR use):

| Setting | VCP value | Description |
|---|---|---|
| **Off** *(default)* | — | No DDC commands sent. |
| **Auto** | 1 | Monitor manages dimming automatically in SDR |
| **Low** | 2 | Low Local Dimming |
| **Standard** | 3 | Standard Local Dimming |
| **High** | 4 | Maximum Local Dimming |

> Right-click tray icon → **Local Dimming SDR (KTC)**

Both settings are independent so you can, for example, use **High** for HDR gaming and **Auto** (or **Off**) for normal desktop use.

### Supported browsers (fullscreen detection)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Requirements

- Windows 10 version 1903 or later (HDR API requirement)
- Administrator privileges (required by the Windows HDR API)
- A monitor that supports HDR

### Building from source

Requires Visual Studio Build Tools 2022 or later with the MSVC x64 toolchain.

```bat
build.bat
```

Output: `dist\HDRAutostart.exe`

---

## Español

### El problema

Windows 11 soporta HDR, pero no lo activa automáticamente. Si dejas el HDR siempre encendido, el escritorio, el navegador y las aplicaciones SDR se ven deslavados. Si lo dejas apagado, tienes que abrir la Configuración de pantalla cada vez que quieres jugar o ver un video HDR. HDRAutostart resuelve esto monitoreando el sistema en segundo plano y cambiando el HDR por ti.

### Funcionalidades

| Función | Descripción |
|---|---|
| **Detección automática de juegos** | Monitorea Steam / GOG / Epic (y cualquier carpeta personalizada) en busca de nuevos procesos de juego y activa el HDR en cuanto el juego abre. El HDR se desactiva automáticamente cuando el juego cierra. |
| **Pantalla completa en navegador** | Cuando Chrome, Edge, Firefox, Brave, Vivaldi, Opera u otros navegadores compatibles entran en pantalla completa (F11 / video en pantalla completa), se activa el HDR. Se desactiva en cuanto el navegador sale de pantalla completa. |
| **Lista blanca** | Agrega archivos `.exe` individuales que siempre deben activar el HDR, independientemente de su carpeta. |
| **Lista negra** | Agrega archivos `.exe` individuales que nunca deben activar el HDR (p. ej. launchers dentro de carpetas de juegos que no quieres que disparen el HDR). |
| **Local Dimming KTC** | Para monitores KTC con soporte DDC/CI: controla automáticamente el nivel de Local Dimming del monitor mediante su comando VCP interno cuando el HDR se activa. Desactivado por defecto — seguro en cualquier monitor. |
| **Ejecutar al inicio** | Opción con un clic para lanzar HDRAutostart con Windows. |
| **Bandeja del sistema** | Se ejecuta silenciosamente en segundo plano. Icono naranja = HDR activo, icono gris = HDR inactivo. Clic derecho para el menú. |

### Instalación

1. Descarga `HDRAutostart.exe` desde [Releases](../../releases).
2. Ejecútalo. Windows pedirá privilegios de administrador — son necesarios para controlar el HDR a través de la API de Windows.
3. La app aparece en la bandeja del sistema. Clic derecho para configurar.
4. (Opcional) Activa **Ejecutar al inicio** para que arranque automáticamente con Windows.

### Configuración

Todos los ajustes se guardan en `hdrautostart.ini` junto al ejecutable.

#### Carpetas monitoreadas

HDRAutostart viene pre-configurado con la ruta de tu biblioteca de Steam (leída del registro). Cualquier `.exe` que se lance desde dentro de una carpeta monitorizada activará el HDR. Puedes agregar carpetas de GOG, Epic o cualquier carpeta de juegos personalizada.

> Clic derecho en el icono de bandeja → **Carpetas monitoreadas…**

#### Activar HDR siempre (Lista blanca)

Agrega archivos `.exe` específicos que siempre deben activar el HDR sin importar dónde estén ubicados. Útil para juegos instalados fuera de tus carpetas de biblioteca habituales.

> Clic derecho en el icono de bandeja → **Activar HDR siempre…**

#### Nunca activar HDR (Lista negra)

Agrega archivos `.exe` que nunca deben activar el HDR. Útil para launchers (p. ej. `EpicGamesLauncher.exe`) que viven dentro de una carpeta monitorizada pero no son juegos.

> Clic derecho en el icono de bandeja → **Nunca activar HDR…**

#### Local Dimming KTC (solo monitores KTC)

HDRAutostart envía comandos DDC/CI a los monitores KTC para controlar el Local Dimming automáticamente cada vez que el HDR se activa o desactiva. Hay dos ajustes independientes, uno para cada modo:

**Local Dimming HDR (KTC)** — nivel que se aplica cuando el HDR está activo (p. ej. mientras juegas):

| Ajuste | Valor VCP | Descripción |
|---|---|---|
| **Desactivado** *(por defecto)* | — | No se envían comandos DDC. Seguro para todos los monitores. |
| **Auto** | 1 | El monitor controla el dimming automáticamente |
| **Bajo** | 2 | Local Dimming bajo |
| **Estándar** | 3 | Local Dimming estándar |
| **Alto** | 4 | Local Dimming máximo — recomendado para jugar en HDR en KTC |

> Clic derecho en el icono de bandeja → **Local Dimming HDR (KTC)**

**Local Dimming SDR (KTC)** — nivel que se aplica cuando el HDR se desactiva (escritorio normal, uso en SDR):

| Ajuste | Valor VCP | Descripción |
|---|---|---|
| **Desactivado** *(por defecto)* | — | No se envían comandos DDC. |
| **Auto** | 1 | El monitor gestiona el dimming automáticamente en SDR |
| **Bajo** | 2 | Local Dimming bajo |
| **Estándar** | 3 | Local Dimming estándar |
| **Alto** | 4 | Local Dimming máximo |

> Clic derecho en el icono de bandeja → **Local Dimming SDR (KTC)**

Ambos ajustes son independientes, por lo que puedes usar, por ejemplo, **Alto** para jugar en HDR y **Auto** (o **Desactivado**) para el uso normal del escritorio.

### Navegadores compatibles (detección pantalla completa)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Requisitos

- Windows 10 versión 1903 o posterior (requisito de la API HDR)
- Privilegios de administrador (requeridos por la API HDR de Windows)
- Un monitor compatible con HDR

### Compilar desde el código fuente

Requiere Visual Studio Build Tools 2022 o posterior con la toolchain MSVC x64.

```bat
build.bat
```

Resultado: `dist\HDRAutostart.exe`

---

## License

MIT
