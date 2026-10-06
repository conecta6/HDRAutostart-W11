# HDRAutostart-W11 — Contexto para IA

## Qué es este proyecto

Utilidad para la bandeja del sistema de Windows 11 escrita en C++ que habilita y deshabilita HDR automáticamente según la actividad del usuario. Detecta cuando se lanza un juego que tiene perfil (o se pone un navegador en pantalla completa), activa HDR, y lo desactiva al salir. Solo trabaja por perfiles de juego: un programa sin perfil se ignora.

**Problema que resuelve:** Windows 11 soporta HDR pero no lo alterna automáticamente. Tenerlo siempre activo hace que apps SDR se vean deslavadas; apagarlo obliga a activarlo manualmente cada vez.

---

## Stack técnico

- **Lenguaje:** C++ (C++11), ~3300 líneas en un solo `.cpp`
- **Compilador:** MSVC (Visual Studio Build Tools, workload "Desktop development with C++")
- **API de HDR:** Windows DisplayConfig/CCD (`SetDisplayConfig`, `QueryDisplayConfig`)
- **Control de monitor:** DDC/CI via VCP codes (Windows `CapabilitiesRequestAndCapabilitiesReply`, `GetVCPFeatureAndVCPFeatureReply`, `SetVCPFeature`)
- **Instalador:** NSIS 3.x
- **Configuración:** archivo `.ini`; carpeta indicada por `ConfigPath` en el registro de Windows (la escribe el instalador)

---

## Archivos clave

| Archivo | Rol |
|---------|-----|
| `hdrautostart.cpp` | Aplicación principal. Toda la lógica está aquí. |
| `testhdr.cpp` | Utilidad de pruebas standalone para HDR y detección de procesos. |
| `build.bat` | Script de compilación con MSVC. Localiza el compilador con `vswhere` (cualquier Visual Studio/Build Tools) y `makensis` en PATH. Genera icono, compila, enlaza recursos, llama a NSIS. |
| `create_icon.ps1` | Genera el `.ico` multi-tamaño (16/32/48/256 px) con texto "HDR". |
| `installer.nsi` | Instalador NSIS. Soporta instalación global (Program Files) y por usuario (AppData). Crea la tarea programada de inicio. Usa `SetRegView 64`. |
| `hdrautostart.rc` | Resource script — embebe el icono en el ejecutable. |
| `README.md` | Documentación de usuario en inglés y español. |

---

## Funcionalidades implementadas

### Core HDR
- Trabaja SOLO por perfiles de juego (`GameProfile`): un programa solo se vigila si tiene perfil. Ya no hay carpetas monitorizadas, whitelist, blacklist ni exclude, ni lista interna de lanzadores ignorados
- Perfil = entrada (`exe`) + `hdr` (sí/no) + `localDimming` (0 = no tocar, 1-4) + `sharpness` (-1 = no tocar, 0-10) + `brightness` (0-100; solo se envía en perfiles sin HDR). Cada perfil lleva sus valores propios: ya no existe "usar el valor general" (-1 en dimming/sharpness solo se lee al migrar un `.ini` antiguo). `ClassifyProcess` devuelve 1 (perfil con HDR: `MonitorThread` enciende HDR mientras el juego está abierto y envía atenuación y nitidez del perfil, sin brillo), -1 (perfil sin HDR = juego SDR: no enciende HDR, envía atenuación/nitidez/brillo del perfil) o 0 (sin perfil: se ignora)
- Los únicos ajustes generales (`Config`) son los de escritorio (`ktcDimmingDesktop`, `ktcSharpnessDesktop`, `ktcBrightnessDesktop`): a ellos vuelve el monitor cuando no queda ningún juego ni vídeo HDR de navegador (`ApplyDesktopValues`, `DesktopProfile`). Defaults de instalación nueva: atenuación 1 (Auto), nitidez 6, brillo 22. Un perfil nuevo (diálogo Agregar) arranca con HDR marcado y los valores de escritorio como base; si ya hay perfil para ese `.exe` se edita ese, y si lo cubre un perfil por nombre/carpeta (solo `.ini`) parte de sus valores
- Cambios en caliente (`MonitorThread`): `g_profilesGen` (sube al crear/editar/borrar un perfil) hace que el hilo reclasifique los procesos: se vacía `seen` (un juego ya abierto que gana perfil se detecta), se sueltan los juegos cuya clase cambió (a 0 o entre HDR y SDR) y las transiciones las hacen los bloques de "juegos cerrados"; si el perfil conserva su clase se reenvían sus valores solo con un único juego abierto (HDR o SDR en total) y solo si el modo coincide con `hdrActive`; con varios, rige desde el siguiente arranque. `g_desktopGen` (sube al cambiar un valor de escritorio desde el menú, `DesktopSettingsChanged`) envía los valores de escritorio al momento solo si no hay juego HDR/SDR, `hdrActive` ni `g_browserHdrOn` (y ya pasó el primer escaneo). Los valores de vídeo no tienen hot-apply: rigen en la siguiente transición
- La entrada de un perfil puede ser ruta completa, nombre de ejecutable o prefijo de carpeta (terminado en `\`); prioridad ruta > nombre > carpeta, y entre carpetas gana la más larga (`MatchProfileEntry`, `FindProfile`). La interfaz solo crea rutas completas; las otras dos formas solo editando el `.ini`
- Al arrancar, el primer escaneo también detecta juegos ya abiertos y apaga un HDR que hubiera quedado encendido sin juego
- HDR espera 2 s (`kHdrOffGraceMs`) tras cerrarse el último juego con HDR antes de apagarse
- Detección de pantalla completa en navegadores (Chrome, Edge, Firefox, Brave, Vivaldi, Opera, etc.). Desactivado por defecto: interruptor en el menú Vídeo → "HDR on browser fullscreen" (`browser_hdr` en el `.ini`, `browserHdrEnabled`)
- `SetHDR`/`IsHDROn` solo actúan sobre pantallas KTC (`IsKTCDisplayPath`); otros monitores no se tocan
- Menú de bandeja (`TrayWndProc`, `WM_TRAYICON`): cabecera con la versión, "Game profiles..." / "Perfiles de juego...", submenú "Desktop (KTC)" / "Escritorio (KTC)" (Local Dimming con "Don't change"/"No tocar" (valor 0: no envía nada, no apaga)/Auto/Low/Standard/High, "Sharpness: <valor>...", "Brightness: <n>..."), submenú "Video" / "Vídeo" ("HDR on browser fullscreen" con marca + su propio Local Dimming + "Sharpness: <valor>..."), "Run at startup", "GitHub", "Exit". Ya no existe el submenú "KTC Settings". Diálogo de perfil ("Profile settings" / "Ajustes del perfil", `ProfEditDlgProc`): casilla "Enable HDR for this game" (`profHdrCheck`), combo "Local Dimming:" ("Don't change"..High), combo "Sharpness:" ("Don't change" / "No tocar", 0-10; cadena `ktcKeep`) y "Brightness (0-100):" (edit + spin; atenuado mientras HDR está marcado)

### KTC Monitor (DDC/CI via VCP codes)
El proyecto solo funciona con monitores KTC. Se reconocen por ID de fabricante PnP "KTC" o "SKG" (`IsKTCDeviceId`, `IsKTCDisplayPath`) o, si no, porque responden al código propio de atenuación local `0xF4` con máximo distinto de 0 (`RespondsToKTCDimming`). Control granular:
- **Local Dimming** (VCP `0xF4`) — tres juegos de valores: escritorio, vídeo de navegador (`videoDimming`) y cada perfil
- **Sharpness** (VCP `0x87`) — igual: escritorio, vídeo (`videoSharpness`) y cada perfil. Tras cada cambio de HDR se reenvía (el cambio lo reinicia)
- **Brightness** (VCP `0x10`) — escritorio y perfiles sin HDR; con HDR activo no se envía (lo gestiona el monitor). El vídeo de navegador no tiene brillo
- **Perfiles por juego** — cada perfil lleva atenuación, nitidez y brillo propios, sin herencia en ejecución (heredan los de escritorio solo al crearse). Quitados los ajustes generales separados para juegos HDR / SDR (atenuación HDR/SDR, nitidez HDR/SDR, brillo SDR)
- **Vídeo HDR en navegador a pantalla completa** — dos valores propios (atenuación y nitidez) en el submenú Vídeo; `CheckBrowserHDR` los envía al activar HDR y al salir aplica los de escritorio

### Sistema
- Inicio automático via tarea programada de Windows (elevada, sin prompt UAC al arrancar). En instalación global (`SetStartup`, `installer.nsi`) la tarea se registra para el grupo Users por SID (`S-1-5-32-545`), no por nombre (`BUILTIN\Users` está localizado y falla en Windows en español); en instalación por usuario usa `schtasks /ru <usuario>`
- Icono en bandeja: naranja = HDR activo, gris = HDR inactivo
- Modo portable (sin instalador) y modo instalado con migración de config
- Actualización automática: `UpdateCheckThread` consulta GitHub al arrancar y `DoSilentUpdate` lanza el instalador con `/S`. Solo si `IsInstalledCopy()`; las copias portables NO se actualizan solas

---

## Configuración (hdrautostart.ini)

Ubicación (`ConfigDir()`): ruta `ConfigPath` del registro (HKLM, luego HKCU). Instalación global → `%ProgramData%\HDRAutostart`; por usuario → `%APPDATA%\HDRAutostart`; portable → junto al ejecutable. Sin clave de registro, `ConfigDir()` prueba antes `%APPDATA%\HDRAutostart` y `%LOCALAPPDATA%\HDRAutostart` si existen, y al final `ExeDir()`.

**Registro:**
- `HKEY_LOCAL_MACHINE\Software\HDRAutostart` (instalación global)
- `HKEY_CURRENT_USER\Software\HDRAutostart` (instalación por usuario)

**Secciones del .ini:**
- `[settings]` (orden de `SaveConfig`) — `ktc_dimming_desktop` (0-4), `ktc_sharpness_desktop` (-1..10), `ktc_brightness_desktop` (0-100), `video_dimming` (0-4), `video_sharpness` (-1..10), `browser_hdr` (0/1), timestamp de última actualización (`last_update_attempt`, solo si no es 0). Ya no se escriben `ktc_local_dimming`, `ktc_sdr_local_dimming`, `ktc_sharpness_hdr`, `ktc_sharpness_sdr` ni `ktc_brightness_sdr` (solo se leen al migrar)
- `[profiles]` — una línea por perfil: `exe|dimming|sharpness|hdr|brightness` (`hdr` 1/0; dimming 0-4; sharpness -1/0-10; brightness 0-100, ignorado si `hdr`=1 pero siempre escrito). `exe` en minúsculas: ruta completa, nombre de ejecutable o prefijo de carpeta terminado en `\`. Es la única sección de juegos: ya no se escriben `[folders]`, `[whitelist]`, `[blacklist]` ni `[exclude]`. Ejemplo: `eldenring.exe|4|-1|1|22`

**Migración de un `.ini` antiguo (`LoadConfig`):** si encuentra `[folders]`/`[whitelist]`/`[blacklist]`/`[exclude]` o perfiles de 3 campos (sin `hdr`), convierte y reescribe el archivo. Whitelist → perfiles con HDR; blacklist → perfiles SDR (la blacklist se procesa primero, antes ganaba a la whitelist; si la entrada ya tiene perfil no se duplica). Perfiles de 3 campos: conservan dimming/sharpness y el modo se deduce (coincide con blacklist → SDR; coincide con whitelist o carpeta monitorizada → HDR; si no, SDR). Carpetas y exclusiones se descartan. Antes de reescribir se guarda `hdrautostart.ini.bak` (nunca sobrescribe una copia existente; si no se puede copiar, el archivo no se reescribe y la migración se repite en el siguiente arranque). `LoadConfig` solo se ejecuta al arrancar; `SaveConfig` escribe toda la config en memoria

**Migración del modelo de ajustes (`LoadConfig`):** si el `.ini` contiene alguna clave general antigua (`ktc_local_dimming`, `ktc_sdr_local_dimming`, `ktc_sharpness_hdr`, `ktc_sharpness_sdr`, `ktc_brightness_sdr`; `sawOldKey`), los perfiles con -1 en dimming/sharpness ("valor general") toman el general antiguo de su modo (HDR o SDR), los perfiles sin campo de brillo toman el antiguo brillo SDR, y los valores de vídeo toman los antiguos de HDR. Sin claves antiguas no se rellena nada: en el formato nuevo un -1 de nitidez significa "No tocar". Un `.ini` sin `ktc_dimming_desktop` se respalda en `hdrautostart.ini.bak` (sin sobrescribir uno existente) antes de reescribirse; si la copia falla, queda pendiente (`g_bakPending`) y `SaveConfig` la reintenta. Los campos numéricos no válidos no se leen como 0: el brillo vuelve a su valor por defecto y una línea de perfil con atenuación, nitidez o hdr inválidos se descarta y se registra en el log. Si la migración descarta carpetas de `[folders]`, se muestra un globo de aviso en la bandeja.

---

## Cómo compilar

```bat
build.bat
```

Requiere:
1. Visual Studio Build Tools (cualquier versión, localizado con `vswhere`) con "Desktop development with C++"
2. NSIS 3.x en el PATH (para generar el instalador; si no, prueba las carpetas por defecto)
3. PowerShell disponible (para generar el icono)

---

## Estado actual del proyecto

### Versión: 0.35 (`APP_VERSION` en `hdrautostart.cpp`)

### Últimos cambios (ver `git log` para detalle):
- `67d0c04` — refactor process matching logic to support folder prefix and basename comparisons
- `17dd86c` — refactor MonitorThread to improve process tracking and cleanup of dead PIDs
- `5fff4f1` — add browser HDR toggle and menu integration
- `2c874dc` — refactor SDR dimming logic in MonitorThread to simplify state handling
- `a9da94a` — add support for tracking SDR dimming application state in MonitorThread
- `53a1506` — enhance SDR game profile handling for local dimming and sharpness settings
- `91af1ed` — 0.35
- `7e913d5` — fix multisuaurio
- `b24ff50` — registro de 64 bits y creación de la tarea programada: `build.bat` (vswhere, búsqueda de makensis), `create_icon.ps1` (`$ErrorActionPreference = 'Stop'`), `installer.nsi` (`SetRegView 64`, limpieza de la vista 32 bits, nombre de usuario leído en instalación, aviso si falla la tarea programada, grupo Users por SID) y `hdrautostart.cpp` (solo KTC, espera de 2 s para apagar HDR, comprobación de HDR dejado encendido, actualización solo en copias instaladas)
- Sin commit (working tree): `hdrautostart.cpp`, `README.md`, `CLAUDE.md` e `installer.nsi` (solo un comentario). (1) La app trabaja solo por perfiles de juego (se eliminan carpetas monitorizadas, whitelist, blacklist, exclude y la lista de lanzadores ignorados con sus menús y diálogos), `GameProfile` gana `hdr`, "Game profiles..." sube al menú principal y `LoadConfig` migra los `.ini` antiguos. (2) Nuevo modelo de ajustes: los generales son solo los de escritorio (submenú "Desktop (KTC)"), cada perfil lleva atenuación, nitidez, brillo y HDR propios (`GameProfile.brightness`, formato `exe|dimming|sharpness|hdr|brightness`), el vídeo de navegador tiene `videoDimming`/`videoSharpness`, desaparece el submenú "KTC Settings" y los ajustes generales HDR/SDR, y `g_profilesGen`/`g_desktopGen` aplican en caliente los cambios de perfil y de escritorio

### Trabajo en curso / pendiente:
<!-- Actualizar esta sección al comenzar/terminar trabajo significativo -->
- Unificar el HDR del navegador con el de los juegos: hoy `CheckBrowserHDR` corre en el hilo de la bandeja (la congela 1-2 s en cada transición) y no comparte estado con `MonitorThread`, así que al salir de un vídeo a pantalla completa con un juego SDR abierto se aplican los valores de escritorio en vez de los del juego. Arreglo previsto: que el temporizador solo detecte y `MonitorThread` haga los cambios. Es una reforma, pendiente de decisión.
- Probar en Windows con NSIS los cambios del instalador (SetRegView 64, nombre de usuario en tiempo de instalación) antes de publicar
- Probar en Windows la migración de un .ini antiguo (listas → perfiles) y el nuevo diálogo de perfil
- Probar en Windows con el monitor KTC el modelo nuevo de ajustes (escritorio / perfil / vídeo) y la migración del .ini

### Decisiones de diseño conocidas:
- Toda la lógica en un único `.cpp` — decisión deliberada para mantener simplicidad y portabilidad
- Sin dependencias externas — solo Windows SDK
- Solo se actúa sobre monitores KTC: HDR (`SetHDR`, `IsHDROn`) y VCP (brillo, nitidez, atenuación local) se aplican solo a pantallas KTC; los demás monitores conectados no se tocan. Si no hay ninguna pantalla KTC con HDR, no se hace nada (se registra en el log)
- La app trabaja solo por perfiles de juego (decisión del dueño, 2026-10-06: las carpetas y listas eran confusas y heterogéneas). Un programa sin perfil no se vigila
- Los ajustes generales son solo los de escritorio y cada perfil lleva sus propios valores (atenuación, nitidez, brillo, HDR), heredando los de escritorio al crearse (decisión del dueño, 2026-10-06). Antes había generales separados para juegos HDR y SDR, con "usar valor general" en los perfiles: se eliminó. Tras crearse, un perfil no sigue los cambios del escritorio
- Las copias portables no se actualizan solas (el instalador silencioso crearía una instalación aparte y dejaría atrás la config portable)

---

## Notas para la IA

- El usuario habla español; responder en español.
- El código es C++ con Windows API pura — sin frameworks modernos ni abstracciones.
- Al sugerir cambios, mantener el estilo existente: funciones globales, sin clases innecesarias, comentarios en inglés dentro del código.
- Antes de proponer refactors, preguntar — el usuario prefiere cambios quirúrgicos y enfocados.
