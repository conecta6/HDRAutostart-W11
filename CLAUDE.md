# HDRAutostart-W11 — Contexto para IA

## Qué es este proyecto

Utilidad para la bandeja del sistema de Windows 11 escrita en C++ que habilita y deshabilita HDR automáticamente según la actividad del usuario. Detecta cuando se lanza un juego o se pone un navegador en pantalla completa, activa HDR, y lo desactiva al salir.

**Problema que resuelve:** Windows 11 soporta HDR pero no lo alterna automáticamente. Tenerlo siempre activo hace que apps SDR se vean deslavadas; apagarlo obliga a activarlo manualmente cada vez.

---

## Stack técnico

- **Lenguaje:** C++ (C++11), ~3000 líneas en un solo `.cpp`
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
- Detección automática de juegos (monitorea carpetas de Steam, GOG, Epic y personalizadas). Al arrancar, el primer escaneo también detecta juegos ya abiertos y apaga un HDR que hubiera quedado encendido sin juego
- HDR espera 2 s (`kHdrOffGraceMs`) tras cerrarse el último juego antes de apagarse
- Detección de pantalla completa en navegadores (Chrome, Edge, Firefox, Brave, Vivaldi, Opera, etc.). Desactivado por defecto: interruptor en el menú Vídeo → "HDR on browser fullscreen" (`browser_hdr` en el `.ini`, `browserHdrEnabled`)
- `SetHDR`/`IsHDROn` solo actúan sobre pantallas KTC (`IsKTCDisplayPath`); otros monitores no se tocan
- **Whitelist** — siempre activar HDR para estos `.exe`
- **Blacklist** — nunca activar HDR para estos `.exe`
- **Exclude list** — ignorar completamente ciertos ejecutables o carpetas

### KTC Monitor (DDC/CI via VCP codes)
El proyecto solo funciona con monitores KTC. Se reconocen por ID de fabricante PnP "KTC" o "SKG" (`IsKTCDeviceId`, `IsKTCDisplayPath`) o, si no, porque responden al código propio de atenuación local `0xF4` con máximo distinto de 0 (`RespondsToKTCDimming`). Control granular:
- **Local Dimming** (VCP `0xF4`) — valores separados para modo HDR y modo SDR
- **Sharpness** (VCP `0x87`) — valores separados para HDR, SDR y escritorio
- **Brightness** (VCP `0x10`) — para juegos SDR y escritorio
- **Perfiles por juego** — overrides de Local Dimming y Sharpness por `.exe`

### Sistema
- Inicio automático via tarea programada de Windows (elevada, sin prompt UAC al arrancar)
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
- `[settings]` — valores KTC de brightness/sharpness/dimming, `browser_hdr`, timestamp de última actualización (`last_update_attempt`)
- `[folders]` — carpetas de juegos a monitorear
- `[whitelist]` — ejecutables que siempre activan HDR
- `[blacklist]` — ejecutables que nunca activan HDR
- `[exclude]` — carpetas/archivos completamente ignorados
- `[profiles]` — overrides por juego: `exe|dimming|sharpness`

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
- Sin commit (working tree): `build.bat` (vswhere, búsqueda de makensis), `create_icon.ps1` (`$ErrorActionPreference = 'Stop'`), `installer.nsi` (`SetRegView 64`, limpieza de la vista 32 bits, nombre de usuario leído en instalación, aviso si falla la tarea programada) y cambios en `hdrautostart.cpp` (solo KTC, espera de 2 s para apagar HDR, comprobación de HDR dejado encendido, actualización solo en copias instaladas)

### Trabajo en curso / pendiente:
<!-- Actualizar esta sección al comenzar/terminar trabajo significativo -->
- Unificar el HDR del navegador con el de los juegos: hoy `CheckBrowserHDR` corre en el hilo de la bandeja (la congela 1-2 s en cada transición) y no comparte estado con `MonitorThread`, así que al salir de un vídeo a pantalla completa con un juego SDR abierto se aplican los valores de escritorio en vez de los del juego. Arreglo previsto: que el temporizador solo detecte y `MonitorThread` haga los cambios. Es una reforma, pendiente de decisión.
- Probar en Windows con NSIS los cambios del instalador (SetRegView 64, nombre de usuario en tiempo de instalación) antes de publicar

### Decisiones de diseño conocidas:
- Toda la lógica en un único `.cpp` — decisión deliberada para mantener simplicidad y portabilidad
- Sin dependencias externas — solo Windows SDK
- Solo se actúa sobre monitores KTC: HDR (`SetHDR`, `IsHDROn`) y VCP (brillo, nitidez, atenuación local) se aplican solo a pantallas KTC; los demás monitores conectados no se tocan. Si no hay ninguna pantalla KTC con HDR, no se hace nada (se registra en el log)
- Las copias portables no se actualizan solas (el instalador silencioso crearía una instalación aparte y dejaría atrás la config portable)

---

## Notas para la IA

- El usuario habla español; responder en español.
- El código es C++ con Windows API pura — sin frameworks modernos ni abstracciones.
- Al sugerir cambios, mantener el estilo existente: funciones globales, sin clases innecesarias, comentarios en inglés dentro del código.
- Antes de proponer refactors, preguntar — el usuario prefiere cambios quirúrgicos y enfocados.
