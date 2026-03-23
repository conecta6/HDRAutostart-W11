# HDRAutostart-W11 — Contexto para IA

## Qué es este proyecto

Utilidad para la bandeja del sistema de Windows 11 escrita en C++ que habilita y deshabilita HDR automáticamente según la actividad del usuario. Detecta cuando se lanza un juego o se pone un navegador en pantalla completa, activa HDR, y lo desactiva al salir.

**Problema que resuelve:** Windows 11 soporta HDR pero no lo alterna automáticamente. Tenerlo siempre activo hace que apps SDR se vean deslavadas; apagarlo obliga a activarlo manualmente cada vez.

---

## Stack técnico

- **Lenguaje:** C++ (C++11), ~2600 líneas en un solo `.cpp`
- **Compilador:** MSVC (Visual Studio Build Tools, workload "Desktop development with C++")
- **API de HDR:** Windows DisplayConfig/CCD (`SetDisplayConfig`, `QueryDisplayConfig`)
- **Control de monitor:** DDC/CI via VCP codes (Windows `CapabilitiesRequestAndCapabilitiesReply`, `GetVCPFeatureAndVCPFeatureReply`, `SetVCPFeature`)
- **Instalador:** NSIS 3.x
- **Configuración:** archivo `.ini` con ruta almacenada en registro de Windows

---

## Archivos clave

| Archivo | Rol |
|---------|-----|
| `hdrautostart.cpp` | Aplicación principal. Toda la lógica está aquí. |
| `testhdr.cpp` | Utilidad de pruebas standalone para HDR y detección de procesos. |
| `build.bat` | Script de compilación con MSVC. Genera icono, compila, enlaza recursos, llama a NSIS. |
| `create_icon.ps1` | Genera el `.ico` multi-tamaño (16/32/48/256 px) con texto "HDR". |
| `installer.nsi` | Instalador NSIS. Soporta instalación global (Program Files) y por usuario (AppData). |
| `hdrautostart.rc` | Resource script — embebe el icono en el ejecutable. |
| `README.md` | Documentación de usuario en inglés y español. |

---

## Funcionalidades implementadas

### Core HDR
- Detección automática de juegos (monitorea carpetas de Steam, GOG, Epic y personalizadas)
- Detección de pantalla completa en navegadores (Chrome, Edge, Firefox, Brave, Vivaldi, Opera, etc.)
- **Whitelist** — siempre activar HDR para estos `.exe`
- **Blacklist** — nunca activar HDR para estos `.exe`
- **Exclude list** — ignorar completamente ciertos ejecutables o carpetas

### KTC Monitor (DDC/CI via VCP codes)
El proyecto tiene soporte especial para monitores KTC con control granular:
- **Local Dimming** (VCP `0xF4`) — valores separados para modo HDR y modo SDR
- **Sharpness** (VCP `0x87`) — valores separados para HDR, SDR y escritorio
- **Brightness** (VCP `0x10`) — para juegos SDR y escritorio
- **Perfiles por juego** — overrides de Local Dimming y Sharpness por `.exe`

### Sistema
- Inicio automático via tarea programada de Windows (elevada, sin prompt UAC al arrancar)
- Icono en bandeja: naranja = HDR activo, gris = HDR inactivo
- Modo portable (sin instalador) y modo instalado con migración de config

---

## Configuración (hdrautostart.ini)

Almacenado junto al ejecutable o en la ruta indicada por `ConfigPath` en el registro.

**Registro:**
- `HKEY_LOCAL_MACHINE\Software\HDRAutostart` (instalación global)
- `HKEY_CURRENT_USER\Software\HDRAutostart` (instalación por usuario)

**Secciones del .ini:**
- `[settings]` — valores KTC de brightness/sharpness/dimming, timestamp de última actualización
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
1. Visual Studio Build Tools con "Desktop development with C++"
2. NSIS 3.x en el PATH (para generar el instalador)
3. PowerShell disponible (para generar el icono)

---

## Estado actual del proyecto

### Versión: 0.22

### Últimos cambios (ver `git log` para detalle):
- `4a6f002` — ktc brightness control options
- `c6cba0f` — sharpness to desktop
- `a6613c4` — 0.22
- `ded96e0` — 0.22 sharpness bug fix

### Trabajo en curso / pendiente:
<!-- Actualizar esta sección al comenzar/terminar trabajo significativo -->
- (sin tareas pendientes documentadas actualmente)

### Decisiones de diseño conocidas:
- Toda la lógica en un único `.cpp` — decisión deliberada para mantener simplicidad y portabilidad
- Sin dependencias externas — solo Windows SDK
- El soporte KTC es opcional y se omite silenciosamente si el monitor no lo soporta

---

## Notas para la IA

- El usuario habla español; responder en español.
- El código es C++ con Windows API pura — sin frameworks modernos ni abstracciones.
- Al sugerir cambios, mantener el estilo existente: funciones globales, sin clases innecesarias, comentarios en inglés dentro del código.
- Antes de proponer refactors, preguntar — el usuario prefiere cambios quirúrgicos y enfocados.
