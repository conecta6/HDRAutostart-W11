# HDRAutostart

**Automatic HDR switching for Windows — activate HDR when a game or fullscreen video starts, deactivate it when it ends.**

---

## English

### The problem

Windows 11 supports HDR, but it does not switch automatically. If you leave HDR always-on, your desktop, browser, and SDR apps look washed out. If you leave it off, you have to open Display Settings every time you want to play a game or watch HDR video. HDRAutostart solves this by monitoring your system in the background and toggling HDR for you.

> **Supported monitors only:** HDRAutostart switches HDR on and off, and sends monitor settings, **only to a KTC monitor or to the ASUS ROG Strix XG27AQWMG**. Any other monitor connected to the PC (for example a laptop panel) is never touched. KTC monitors are recognised by their manufacturer ID ("KTC" or "SKG") or because they answer the KTC local dimming command; the ASUS by its ID ("AUSAA02"). The tray menu and the profile dialog show the settings of the monitor that is connected.

### Features

| Feature | Description |
|---|---|
| **Game detection (by profile)** | Only programs that have a game profile are watched. When one launches, HDR is enabled the moment it starts (if its profile has HDR on). Games already open when the app starts are detected too, and an HDR left on by a previous run is turned off. HDR is disabled automatically 2 seconds after the last HDR game closes. |
| **Browser fullscreen** | When Chrome, Edge, Firefox, Brave, Vivaldi, Opera, or other supported browsers enter fullscreen, HDR is enabled. It is disabled again as soon as the browser leaves fullscreen. Off by default: enable it from the tray menu (**Video** → **HDR on browser fullscreen**). To watch HDR videos, simply put the browser in fullscreen mode — usually by pressing **F11** or clicking the fullscreen button on the video player. Its Local Dimming and Sharpness are set in the **Video** submenu. |
| **Desktop values (KTC)** | The only "general" settings: the Local Dimming, Sharpness and Brightness the monitor goes back to when no game (and no HDR browser video) is active. Changing one from the tray menu applies it at once if nothing is holding the monitor. |
| **KTC Local Dimming** | For KTC monitors with DDC/CI support: set the Local Dimming level (VCP 0xF4) for the desktop, for each game profile and for HDR video in a browser. **Don't change** sends no command (the monitor keeps whatever value it has at that moment, it is not switched off), so it is safe on any monitor. |
| **KTC Sharpness** | Set the monitor sharpness (VCP 0x87) from 0 to 10 (or Don't change = send nothing) for the desktop, for each game profile and for HDR video in a browser. Default: 6. |
| **KTC Brightness** | Set the monitor brightness (VCP 0x10): the Desktop value (default 22) and, in profiles with HDR off, the profile's own value. HDR games leave the brightness to the monitor. |
| **ASUS ROG Strix XG27AQWMG** | Picture mode (GameVisual), brightness, contrast, saturation, sharpness, gamma and color temperature, for the desktop and for each game profile. With HDR on only sharpness is sent; everything else applies to SDR games and the desktop. There is no local dimming on this OLED. |
| **Game profiles** | A profile is an executable plus **HDR on/off**, Local Dimming, Sharpness and Brightness. Every profile carries its own values; there is no general fallback. A new profile starts with HDR on and the Desktop values as a base, and you change them. HDR on: HDR is enabled while the game runs. HDR off (an SDR game): HDR is not enabled, but the profile's dimming, sharpness and brightness are applied. When the last game closes, the monitor returns to the Desktop values. Creating, editing or removing a profile while its game is open takes effect without restarting the game. |
| **Run at startup** | Launches HDRAutostart with Windows through a scheduled task (no UAC prompt). The installer creates it; you can turn it on or off from the tray menu. |
| **System tray** | Runs silently in the background. Orange icon = HDR active, grey icon = HDR inactive. Right-click for the menu. |

### Installation

1. Download `HDRAutostartSetup.exe` from [Releases](../../releases).
2. Run the installer. Windows will ask for administrator privileges — these are required to control HDR via the Windows Display API.
3. The app appears in the system tray. Right-click to configure.
4. The installer already sets HDRAutostart to start with Windows. You can turn this on or off from the tray menu (**Run at startup**). In portable mode (running the `.exe` without installing) it is optional: enable it from that same menu entry.

**Automatic updates:** the installed copy checks GitHub for a new version when it starts and updates itself silently. Portable copies do not update themselves.

### Configuration

All settings are stored in `hdrautostart.ini`. Its location depends on how you installed the app:

- Install for all users: `%ProgramData%\HDRAutostart`
- Install for the current user: `%APPDATA%\HDRAutostart`
- Portable mode (no installer): next to the executable

#### Tray menu reference

The tray menu currently contains these entries:

    HDRAutostart vX.Y.Z              (informational header, disabled)
    Game profiles...
    Desktop (KTC)
      Local Dimming
        Don't change / Auto / Low / Standard / High
      Sharpness: <current value>...
      Brightness: <current value>...
    Video
      HDR on browser fullscreen      (checked when enabled)
      Local Dimming
        Don't change / Auto / Low / Standard / High
      Sharpness: <current value>...
    Run at startup                   (checked when enabled)
    GitHub
    Exit

- `Game profiles...` opens the game profile manager (see below).
- `Desktop (KTC)` holds the values the monitor goes back to when no game (and no HDR browser video) is active. `Local Dimming` shows a check mark on the active value; `Sharpness` and `Brightness` show the current value and open the picker / numeric input. A change applies at once if no game or HDR video is holding the monitor.
- `Video -> HDR on browser fullscreen` turns browser fullscreen HDR on or off (off by default).
- `Video -> Local Dimming` and `Video -> Sharpness` are the values used while an HDR video plays in a fullscreen browser. They apply the next time a video goes fullscreen.
- `Run at startup` toggles Windows startup registration.
- `GitHub` opens the project repository in your browser.
- `Exit` closes HDRAutostart.

#### Game profiles

HDRAutostart only watches programs that have a game profile. A profile is an executable plus:

- **Enable HDR for this game** — checked: HDR is turned on while the game is open. Unchecked: SDR game — HDR is not enabled, but the profile's own values are applied.
- **Local Dimming** (field *Local Dimming:*) — Don't change (nothing is sent: the monitor keeps whatever value it has), Auto, Low, Standard or High.
- **Sharpness** — Don't change (nothing is sent: the monitor keeps whatever value it has) or 0–10.
- **Brightness (0-100)** — only used when HDR is off (greyed out while the HDR box is checked). HDR games leave the brightness to the monitor.

Every profile has its own values; there is no "use the general value" option.

To add a game:

1. Right-click tray icon → **Game profiles…**
2. Click **Add** and pick the game's `.exe`.
3. The **Profile settings** dialog opens with HDR checked and the Desktop values (Local Dimming, Sharpness, Brightness) as a starting point. Change what you need.
4. Click **OK**.

Use **Edit** (or double-click) to change a profile and **Remove** to delete it. Adding an `.exe` that already has a profile opens that profile for editing. When the game closes, the monitor returns to the Desktop values.

Profile changes take effect without restarting the game: a new profile is picked up by a game that is already open, a removed one stops being tracked (HDR goes off after the usual 2 s), and switching HDR on or off in a profile switches it for the running game. Edited values are re-sent straight away only when a single game is open; with several, they apply from the next launch.

#### The `.ini` file

Settings go under `[settings]` and one profile per line under `[profiles]`, as `exe|dimming|sharpness|hdr|brightness|picture mode|contrast|saturation|gamma|color temp` (the last five are the ASUS settings, `-1` = don't change; lines without them are still valid). Dimming is `0` (don't change) or `1`–`4` (Auto, Low, Standard, High); sharpness is `-1` (don't change) or `0`–`10`; hdr is `1` (HDR on) or `0` (SDR game); brightness is `0`–`100` and is only used when hdr is `0` (HDR profiles keep the field but ignore it). In `[settings]`, `ktc_dimming_desktop`, `ktc_sharpness_desktop` and `ktc_brightness_desktop` are the Desktop values, `video_dimming` and `video_sharpness` are the Video values (same scales as above) and `browser_hdr` is the browser fullscreen switch (`1` = on). The ASUS Desktop values are `picture_mode_desktop`, `contrast_desktop`, `saturation_desktop`, `gamma_desktop` and `color_temp_desktop` (see "ASUS values" below). Example:

```ini
[settings]
ktc_dimming_desktop=1
ktc_sharpness_desktop=6
ktc_brightness_desktop=22
video_dimming=4
video_sharpness=6
browser_hdr=0
[profiles]
c:\games\cyberpunk 2077\bin\x64\cyberpunk2077.exe|4|8|1|22
eldenring.exe|4|-1|1|22
d:\retro\|0|-1|0|80
```

The first field can be a full path (the only form the interface creates), an executable name (`eldenring.exe`, matches it wherever it is installed) or a folder prefix ending in `\` (`d:\retro\`, matches every program inside). The last two forms can only be set by editing the `.ini` by hand, with HDRAutostart closed (the app rewrites the file when you change a setting). If several entries match, a full path wins over a name, and a name wins over a folder.

**Upgrading from an earlier version:** older versions detected games through monitored folders (e.g. your whole Steam library) plus whitelist, blacklist and exclude lists, and used general Local Dimming / Sharpness values for HDR and for SDR (plus a general SDR brightness). Those no longer exist: the only general values left are the Desktop ones, and every profile carries its own. On first start the old `.ini` is converted automatically and a copy is saved as `hdrautostart.ini.bak` (an existing `.bak` is never overwritten):

- Whitelist entries become profiles with HDR on, blacklist entries become profiles with HDR off, existing profiles keep their values (their HDR on/off is worked out from the old lists), and folders and exclusions are discarded.
- A profile that used the "general value" gets the old general value of its mode (HDR or SDR) for Local Dimming and Sharpness, and every profile takes the old SDR brightness.
- The Video values take the old HDR Local Dimming and Sharpness. Desktop Sharpness and Brightness are kept; Desktop Local Dimming starts at Auto, so set it again from the menu if you want something else.

**Games that were detected only because they were inside a monitored folder stop being detected until you create a profile for them.**

---

### ASUS values (ROG Strix XG27AQWMG only)

With the ASUS connected, **Desktop (ASUS)** and the profile dialog show these settings instead of Local Dimming. Each can be set to **Don't change** (nothing is sent):

| Setting | VCP | Values |
|---------|-----|--------|
| Picture mode | 0xDC | Scenery, Racing, Cinema, RTS/RPG, FPS, sRGB, MOBA, Night Vision, User |
| Brightness | 0x10 | 0–100 |
| Contrast | 0x12 | 0–100 |
| Saturation | 0x8A | 0–100 |
| Sharpness | 0x87 | 0–10 in the app (sent ×10: the monitor uses 0–100) |
| Gamma | 0x72 | 1.8, 2.0, 2.2, 2.4, 2.6 |
| Color temperature | 0x14 | 4000K, 5000K, 6500K, 7500K, 8200K, 9300K, 10000K, User |

Each picture mode keeps its own brightness, gamma, etc., so the picture mode is sent first and the other values after it. With HDR on only sharpness is sent. In the `.ini`, gamma is stored ×10 (`22` = 2.2) and picture mode / color temperature as the VCP value shown by the monitor (e.g. `4` = User picture mode, `5` = 6500K).

### KTC values (KTC monitors only)

> **Note:** HDRAutostart only works with KTC monitors: HDR switching and everything in this section are sent only to a KTC display, and other connected monitors are left alone.

The KTC values live in three places:

> Right-click tray icon → **Desktop (KTC)** — what the monitor goes back to when no game is open
>
> Right-click tray icon → **Video** — HDR video in a fullscreen browser
>
> Right-click tray icon → **Game profiles…** — each game's own values

#### Local Dimming

HDRAutostart sends DDC/CI commands to KTC monitors to set the Local Dimming level (VCP 0xF4). The same five choices are used in all three places:

| Setting | VCP value | Description |
|---|---|---|
| **Don't change** | — | No DDC command sent: the monitor keeps whatever value it has (it does not switch dimming off). Safe for all monitors. |
| **Auto** | 1 | Monitor controls dimming automatically |
| **Low** | 2 | Low Local Dimming |
| **Standard** | 3 | Standard Local Dimming |
| **High** | 4 | Maximum Local Dimming — recommended for HDR gaming on KTC |

Default: **Auto** for Desktop and Video. A new profile copies the Desktop value.

> Desktop (KTC) → **Local Dimming**, Video → **Local Dimming**, or the Local Dimming field of a profile

#### Sharpness

Sets the monitor's sharpness level (VCP 0x87) via DDC/CI. Because switching HDR on or off resets it, the value is sent again after each switch. Select from 0 to 10, or **Don't change** to send no command.

| Setting | Description |
|---|---|
| **Don't change** | No DDC sharpness command sent: the monitor keeps whatever value it has. |
| **0 – 10** | Sharpness level sent to the monitor. Default: **6**. |

> Desktop (KTC) → **Sharpness**, Video → **Sharpness**, or the Sharpness field of a profile

#### Brightness

Sets the monitor's brightness level (VCP 0x10) via DDC/CI, in the 0–100 range. It is sent in two cases only; with HDR on, the monitor handles brightness itself.

| Setting | Default | Description |
|---|---|---|
| **Desktop (KTC)** | 22 | Brightness restored when no game is running. |
| **Profile with HDR off** | Desktop value when created | Brightness applied while that SDR game runs. |

> Desktop (KTC) → **Brightness** (click to open the numeric input), or the Brightness field of a profile

---

### Supported browsers (fullscreen detection)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Windows Defender warning

Some antivirus tools (including Windows Defender) may flag `HDRAutostart.exe` as suspicious. **This is a false positive.** The app is open source — you can review every line of code in this repository. The detection is triggered because the app requests administrator privileges, reads the Windows registry, and controls system APIs, which are the exact features it needs to work. It contains no malicious code.

If you want to verify it yourself, you can [build it from source](#building-from-source) or scan the file at [VirusTotal](https://www.virustotal.com).

### Requirements

- Windows 10 version 1903 or later (HDR API requirement)
- Administrator privileges (required by the Windows HDR API)
- A KTC monitor or an ASUS ROG Strix XG27AQWMG that supports HDR (other monitors are ignored)

### Building from source

Requires:
- [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) (any version) with the **Desktop development with C++** workload (MSVC x64)
- [NSIS 3.x](https://nsis.sourceforge.io/Download)

All source paths in `build.bat` are relative to `%~dp0` (the folder where the script lives). `build.bat` finds the compiler automatically with `vswhere` and looks for `makensis` in your `PATH` (then in the default NSIS folders). Then run:

```bat
build.bat
```

Output: `dist\HDRAutostart.exe` and `dist\HDRAutostartSetup.exe`

---

## Español

### El problema

Windows 11 soporta HDR, pero no lo activa automáticamente. Si dejas el HDR siempre encendido, el escritorio, el navegador y las aplicaciones SDR se ven deslavados. Si lo dejas apagado, tienes que abrir la Configuración de pantalla cada vez que quieres jugar o ver un video HDR. HDRAutostart resuelve esto monitoreando el sistema en segundo plano y cambiando el HDR por ti.

> **Solo monitores compatibles:** HDRAutostart enciende y apaga el HDR, y envía ajustes de monitor, **únicamente a un monitor KTC o al ASUS ROG Strix XG27AQWMG**. Cualquier otro monitor conectado al PC (por ejemplo la pantalla de un portátil) no se toca. Los monitores KTC se reconocen por su ID de fabricante ("KTC" o "SKG") o porque responden al comando de local dimming de KTC; el ASUS por su ID ("AUSAA02"). El menú de la bandeja y el diálogo de perfil muestran los ajustes del monitor conectado.

### Funcionalidades

| Función | Descripción |
|---|---|
| **Detección de juegos (por perfil)** | Solo se vigilan los programas que tienen un perfil de juego. Cuando uno se lanza, el HDR se activa en cuanto abre (si su perfil tiene el HDR activado). También detecta los juegos que ya estaban abiertos al iniciar la app, y apaga un HDR que hubiera quedado encendido de una ejecución anterior. El HDR se desactiva automáticamente 2 segundos después de cerrarse el último juego con HDR. |
| **Pantalla completa en navegador** | Cuando Chrome, Edge, Firefox, Brave, Vivaldi, Opera u otros navegadores compatibles entran en pantalla completa, se activa el HDR. Se desactiva en cuanto el navegador sale de pantalla completa. Está desactivado por defecto: actívalo desde el menú de la bandeja (**Vídeo** → **HDR en navegador a pantalla completa**). Para ver videos en HDR, simplemente pon el navegador en pantalla completa — normalmente pulsando **F11** o el botón de pantalla completa del reproductor de video. Su Local Dimming y su Nitidez se ajustan en el submenú **Vídeo**. |
| **Valores de escritorio (KTC)** | Son los únicos ajustes "generales": el Local Dimming, la Nitidez y el Brillo a los que vuelve el monitor cuando no hay ningún juego (ni vídeo HDR de navegador) activo. Si cambias uno desde el menú de la bandeja, se aplica al momento si nada está controlando el monitor. |
| **Local Dimming KTC** | Para monitores KTC con soporte DDC/CI: fija el nivel de Local Dimming (VCP 0xF4) del escritorio, de cada perfil de juego y del vídeo HDR en navegador. **No tocar** no envía ningún comando (el monitor conserva el valor que tenga en ese momento, no se apaga), así que es seguro en cualquier monitor. |
| **Nitidez KTC** | Fija la nitidez del monitor (VCP 0x87) de 0 a 10 (o No tocar = no enviar nada) para el escritorio, cada perfil de juego y el vídeo HDR en navegador. Valor por defecto: 6. |
| **Brillo KTC** | Fija el brillo del monitor (VCP 0x10): el valor de escritorio (por defecto 22) y, en los perfiles con el HDR desactivado, el valor propio del perfil. Los juegos con HDR dejan el brillo al propio monitor. |
| **ASUS ROG Strix XG27AQWMG** | Modo de imagen (GameVisual), brillo, contraste, saturación, nitidez, gamma y temperatura de color, para el escritorio y para cada perfil de juego. Con HDR activo solo se envía la nitidez; el resto se aplica a los juegos SDR y al escritorio. Este OLED no tiene local dimming. |
| **Perfiles de juego** | Un perfil es un ejecutable más **HDR sí/no**, Local Dimming, Nitidez y Brillo. Cada perfil lleva sus propios valores; no hay valor general de reserva. Un perfil nuevo arranca con el HDR marcado y los valores de escritorio como base, y tú los cambias. HDR sí: el HDR se activa mientras el juego está abierto. HDR no (juego SDR): no se activa el HDR, pero se aplican la atenuación, la nitidez y el brillo del perfil. Al cerrarse el último juego, el monitor vuelve a los valores de escritorio. Crear, editar o borrar un perfil con su juego abierto se aplica sin reiniciar el juego. |
| **Ejecutar al inicio** | Lanza HDRAutostart con Windows mediante una tarea programada (sin aviso de UAC). La crea el instalador; puedes activarla o desactivarla desde el menú de la bandeja. |
| **Bandeja del sistema** | Se ejecuta silenciosamente en segundo plano. Icono naranja = HDR activo, icono gris = HDR inactivo. Clic derecho para el menú. |

### Instalación

1. Descarga `HDRAutostartSetup.exe` desde [Releases](../../releases).
2. Ejecuta el instalador. Windows pedirá privilegios de administrador — son necesarios para controlar el HDR a través de la API de Windows.
3. La app aparece en la bandeja del sistema. Clic derecho para configurar.
4. El instalador ya configura HDRAutostart para que arranque con Windows. Puedes activarlo o desactivarlo desde el menú de la bandeja (**Ejecutar al inicio**). En modo portable (ejecutar el `.exe` sin instalar) es opcional: actívalo desde esa misma entrada del menú.

**Actualizaciones automáticas:** la copia instalada comprueba GitHub al arrancar y se actualiza sola en silencio. Las copias portables no se actualizan solas.

### Configuración

Todos los ajustes se guardan en `hdrautostart.ini`. Su ubicación depende de cómo hayas instalado la app:

- Instalación para todos los usuarios: `%ProgramData%\HDRAutostart`
- Instalación para el usuario actual: `%APPDATA%\HDRAutostart`
- Modo portable (sin instalador): junto al ejecutable

#### Arbol del menu de bandeja

El menu de la bandeja contiene actualmente estas opciones:

    HDRAutostart vX.Y.Z              (cabecera informativa, deshabilitada)
    Perfiles de juego...
    Escritorio (KTC)
      Local Dimming
        No tocar / Auto / Bajo / Estándar / Alto
      Nitidez: <valor actual>...
      Brillo: <valor actual>...
    Vídeo
      HDR en navegador a pantalla completa   (con marca cuando está activado)
      Local Dimming
        No tocar / Auto / Bajo / Estándar / Alto
      Nitidez: <valor actual>...
    Ejecutar al inicio               (con marca cuando está activado)
    GitHub
    Salir

- `Perfiles de juego...` abre el gestor de perfiles de juego (ver más abajo).
- `Escritorio (KTC)` guarda los valores a los que vuelve el monitor cuando no hay ningún juego (ni vídeo HDR de navegador) activo. `Local Dimming` muestra una marca en el valor activo; `Nitidez` y `Brillo` muestran el valor actual y abren el selector / la entrada numérica. Un cambio se aplica al momento si ningún juego ni vídeo HDR está controlando el monitor.
- `Vídeo -> HDR en navegador a pantalla completa` activa o desactiva el HDR en navegador a pantalla completa (desactivado por defecto).
- `Vídeo -> Local Dimming` y `Vídeo -> Nitidez` son los valores que se usan mientras se reproduce un vídeo HDR en un navegador a pantalla completa. Se aplican la próxima vez que un vídeo pase a pantalla completa.
- `Ejecutar al inicio` activa o desactiva el arranque con Windows.
- `GitHub` abre el repositorio del proyecto en el navegador.
- `Salir` cierra HDRAutostart.

#### Perfiles de juego

HDRAutostart solo vigila los programas que tienen un perfil de juego. Un perfil es un ejecutable más:

- **Activar HDR con este juego** — marcada: el HDR se enciende mientras el juego está abierto. Desmarcada: juego SDR — no se activa el HDR, pero se aplican los valores propios del perfil.
- **Atenuación local** — No tocar (no se envía nada: el monitor conserva el valor que tenga), Auto, Bajo, Estándar o Alto.
- **Nitidez** — No tocar (no se envía nada: el monitor conserva el valor que tenga) o 0–10.
- **Brillo (0-100)** — solo se usa con el HDR desactivado (aparece atenuado mientras la casilla de HDR está marcada). Los juegos con HDR dejan el brillo al propio monitor.

Cada perfil tiene sus propios valores; no existe la opción "usar el valor general".

Para añadir un juego:

1. Clic derecho en el icono de bandeja → **Perfiles de juego…**
2. Pulsa **Agregar** y elige el `.exe` del juego.
3. Se abre el diálogo **Ajustes del perfil** con el HDR marcado y los valores de escritorio (Local Dimming, Nitidez, Brillo) como punto de partida. Cambia lo que necesites.
4. Pulsa **Aceptar**.

Usa **Editar** (o doble clic) para cambiar un perfil y **Eliminar** para borrarlo. Si agregas un `.exe` que ya tiene perfil, se abre ese perfil para editarlo. Al cerrar el juego, el monitor vuelve a los valores de escritorio.

Los cambios de perfil se aplican sin reiniciar el juego: un perfil nuevo lo recoge un juego que ya está abierto, uno borrado deja de vigilarse (el HDR se apaga tras los 2 s de siempre) y cambiar el HDR sí/no de un perfil lo cambia también para el juego en marcha. Los valores editados se reenvían al momento solo cuando hay un único juego abierto; con varios, se aplican desde el siguiente arranque.

#### El archivo `.ini`

Los ajustes van en `[settings]` y cada perfil en una línea de `[profiles]`, con el formato `exe|dimming|nitidez|hdr|brillo|modo de imagen|contraste|saturación|gamma|temperatura de color` (los cinco últimos son los ajustes del ASUS, `-1` = no tocar; las líneas sin ellos siguen siendo válidas). Dimming es `0` (no tocar) o `1`–`4` (Auto, Bajo, Estándar, Alto); nitidez es `-1` (no tocar) o `0`–`10`; hdr es `1` (HDR sí) o `0` (juego SDR); brillo es `0`–`100` y solo se usa cuando hdr es `0` (los perfiles con HDR conservan el campo pero lo ignoran). En `[settings]`, `ktc_dimming_desktop`, `ktc_sharpness_desktop` y `ktc_brightness_desktop` son los valores de escritorio, `video_dimming` y `video_sharpness` son los de vídeo (mismas escalas) y `browser_hdr` es el interruptor de pantalla completa en navegador (`1` = activado). Los valores de escritorio del ASUS son `picture_mode_desktop`, `contrast_desktop`, `saturation_desktop`, `gamma_desktop` y `color_temp_desktop` (ver "Valores ASUS" más abajo). Ejemplo:

```ini
[settings]
ktc_dimming_desktop=1
ktc_sharpness_desktop=6
ktc_brightness_desktop=22
video_dimming=4
video_sharpness=6
browser_hdr=0
[profiles]
c:\games\cyberpunk 2077\bin\x64\cyberpunk2077.exe|4|8|1|22
eldenring.exe|4|-1|1|22
d:\retro\|0|-1|0|80
```

El primer campo puede ser una ruta completa (la única forma que crea la interfaz), un nombre de ejecutable (`eldenring.exe`, vale donde esté instalado) o un prefijo de carpeta terminado en `\` (`d:\retro\`, vale para todos los programas de dentro). Las dos últimas formas solo se pueden poner editando el `.ini` a mano, con HDRAutostart cerrado (la app reescribe el archivo cuando cambias un ajuste). Si coinciden varias entradas, la ruta completa gana al nombre, y el nombre gana a la carpeta.

**Si actualizas desde una versión anterior:** las versiones antiguas detectaban los juegos con carpetas monitoreadas (p. ej. toda tu biblioteca de Steam) y listas blanca, negra y de exclusión, y usaban valores generales de Local Dimming / Nitidez para HDR y para SDR (más un brillo SDR general). Ya no existen: los únicos valores generales que quedan son los de escritorio, y cada perfil lleva los suyos. Al arrancar por primera vez, el `.ini` antiguo se convierte solo y se guarda una copia como `hdrautostart.ini.bak` (si ya existe un `.bak`, no se sobrescribe):

- Las entradas de la lista blanca pasan a ser perfiles con HDR sí, las de la lista negra pasan a ser perfiles con HDR no, los perfiles existentes conservan sus valores (su HDR sí/no se deduce de las listas antiguas), y las carpetas y exclusiones se descartan.
- Un perfil que usaba el "valor general" recibe el valor general antiguo de su modo (HDR o SDR) para Local Dimming y Nitidez, y todos los perfiles toman el brillo SDR antiguo.
- Los valores de vídeo toman el Local Dimming y la Nitidez HDR antiguos. La Nitidez y el Brillo de escritorio se conservan; el Local Dimming de escritorio empieza en Auto, así que vuelve a ponerlo desde el menú si quieres otro.

**Los juegos que solo se detectaban por estar dentro de una carpeta monitoreada dejan de detectarse hasta que les crees un perfil.**

---

### Valores ASUS (solo ROG Strix XG27AQWMG)

Con el ASUS conectado, **Escritorio (ASUS)** y el diálogo de perfil muestran estos ajustes en lugar del Local Dimming. Cada uno admite **No tocar** (no se envía nada):

| Ajuste | VCP | Valores |
|--------|-----|---------|
| Modo de imagen | 0xDC | Escenario, Carrera, Cine, RTS/RPG, FPS, sRGB, MOBA, Visión nocturna, Usuario |
| Brillo | 0x10 | 0–100 |
| Contraste | 0x12 | 0–100 |
| Saturación | 0x8A | 0–100 |
| Nitidez | 0x87 | 0–10 en la app (se envía ×10: el monitor usa 0–100) |
| Gamma | 0x72 | 1.8, 2.0, 2.2, 2.4, 2.6 |
| Temperatura de color | 0x14 | 4000K, 5000K, 6500K, 7500K, 8200K, 9300K, 10000K, Usuario |

Cada modo de imagen guarda su propio brillo, gamma, etc., así que primero se envía el modo de imagen y después los demás valores. Con HDR activo solo se envía la nitidez. En el `.ini`, la gamma se guarda ×10 (`22` = 2.2) y el modo de imagen / la temperatura de color como el valor VCP del monitor (p. ej. `4` = modo Usuario, `5` = 6500K).

### Valores KTC (solo monitores KTC)

> **Nota:** HDRAutostart solo funciona con monitores KTC: el cambio de HDR y todo lo de esta sección se envía únicamente a una pantalla KTC, y los demás monitores conectados no se tocan.

Los valores KTC viven en tres sitios:

> Clic derecho en el icono de bandeja → **Escritorio (KTC)** — a qué vuelve el monitor cuando no hay ningún juego abierto
>
> Clic derecho en el icono de bandeja → **Vídeo** — vídeo HDR en un navegador a pantalla completa
>
> Clic derecho en el icono de bandeja → **Perfiles de juego…** — los valores propios de cada juego

#### Local Dimming

HDRAutostart envía comandos DDC/CI a los monitores KTC para fijar el nivel de Local Dimming (VCP 0xF4). Las mismas cinco opciones se usan en los tres sitios:

| Ajuste | Valor VCP | Descripción |
|---|---|---|
| **No tocar** | — | No se envía ningún comando DDC: el monitor conserva el valor que tenga (no apaga la atenuación). Seguro para todos los monitores. |
| **Auto** | 1 | El monitor controla el dimming automáticamente |
| **Bajo** | 2 | Local Dimming bajo |
| **Estándar** | 3 | Local Dimming estándar |
| **Alto** | 4 | Local Dimming máximo — recomendado para jugar en HDR en KTC |

Por defecto: **Auto** en Escritorio y en Vídeo. Un perfil nuevo copia el valor de escritorio.

> Escritorio (KTC) → **Local Dimming**, Vídeo → **Local Dimming**, o el campo Atenuación local de un perfil

#### Nitidez

Ajusta el nivel de nitidez del monitor (VCP 0x87) vía DDC/CI. Como al activar o desactivar el HDR se reinicia, el valor se vuelve a enviar después de cada cambio. Seleccionable de 0 a 10, o **No tocar** para no enviar ningún comando.

| Ajuste | Descripción |
|---|---|
| **No tocar** | No se envía ningún comando de nitidez DDC: el monitor conserva el valor que tenga. |
| **0 – 10** | Nivel de nitidez enviado al monitor. Por defecto: **6**. |

> Escritorio (KTC) → **Nitidez**, Vídeo → **Nitidez**, o el campo Nitidez de un perfil

#### Brillo

Ajusta el nivel de brillo del monitor (VCP 0x10) vía DDC/CI, en el rango 0–100. Solo se envía en dos casos; con el HDR activado, el brillo lo gestiona el propio monitor.

| Ajuste | Por defecto | Descripción |
|---|---|---|
| **Escritorio (KTC)** | 22 | Brillo que se restaura cuando no hay ningún juego en ejecución. |
| **Perfil con HDR desactivado** | Valor de escritorio al crearlo | Brillo aplicado mientras ese juego SDR está en ejecución. |

> Escritorio (KTC) → **Brillo** (haz clic para abrir la entrada numérica), o el campo Brillo de un perfil

---

### Navegadores compatibles (detección pantalla completa)

Chrome, Microsoft Edge, Firefox, Opera, Brave, Vivaldi, Internet Explorer, Waterfox, LibreWolf, Thorium.

### Aviso de Windows Defender

Algunos antivirus (incluido Windows Defender) pueden marcar `HDRAutostart.exe` como sospechoso. **Es un falso positivo.** La aplicación es de código abierto — puedes revisar cada línea de código en este repositorio. La detección se dispara porque la app solicita privilegios de administrador, lee el registro de Windows y controla APIs del sistema, que son exactamente las funciones que necesita para operar. No contiene código malicioso.

Si quieres verificarlo tú mismo, puedes [compilarlo desde el código fuente](#compilar-desde-el-código-fuente) o escanear el archivo en [VirusTotal](https://www.virustotal.com).

### Requisitos

- Windows 10 versión 1903 o posterior (requisito de la API HDR)
- Privilegios de administrador (requeridos por la API HDR de Windows)
- Un monitor KTC o un ASUS ROG Strix XG27AQWMG compatible con HDR (los demás monitores se ignoran)

### Compilar desde el código fuente

Requiere:
- [Visual Studio Build Tools](https://visualstudio.microsoft.com/visual-cpp-build-tools/) (cualquier versión) con la carga de trabajo **Desarrollo para escritorio con C++** (MSVC x64)
- [NSIS 3.x](https://nsis.sourceforge.io/Download)

Todas las rutas en `build.bat` son relativas a `%~dp0` (la carpeta donde vive el script). `build.bat` localiza el compilador automáticamente con `vswhere` y busca `makensis` en tu `PATH` (y después en las carpetas por defecto de NSIS). Luego ejecuta:

```bat
build.bat
```

Resultado: `dist\HDRAutostart.exe` y `dist\HDRAutostartSetup.exe`

---

## License

MIT
