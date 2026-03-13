; =============================================================================
; HDRAutostart -- NSIS Installer
; Build: makensis installer.nsi   (requires NSIS 3.x, https://nsis.sf.net)
; Output: dist\HDRAutostartSetup.exe
; =============================================================================

Unicode True
!define APP_NAME    "HDRAutostart"
!define APP_VERSION "0.22"
!define EXE_NAME    "HDRAutostart.exe"
!define TASK_NAME   "HDRAutostart"
!define REG_APP     "Software\HDRAutostart"
!define REG_UNINST  "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

Name "${APP_NAME} ${APP_VERSION}"
OutFile "dist\HDRAutostartSetup.exe"
Icon   "icon.ico"
RequestExecutionLevel admin

!include "LogicLib.nsh"
!include "MUI2.nsh"

; -- Variables ----------------------------------------------------------------
Var AllUsers     ; "1" = all users, "0" = current user only
Var ProgramData  ; %PROGRAMDATA% path

; -- MUI pages ----------------------------------------------------------------
!define MUI_ABORTWARNING
!define MUI_ICON "icon.ico"
!define MUI_UNICON "icon.ico"
!define MUI_WELCOMEPAGE_TEXT \
    "Este asistente instalara ${APP_NAME} en su equipo.$\n$\n\
HDRAutostart activa el HDR automaticamente al lanzar un juego y lo desactiva \
al cerrarlo, sin necesidad de intervencion manual.$\n$\n\
Haga clic en Siguiente para continuar."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN          "$INSTDIR\${EXE_NAME}"
!define MUI_FINISHPAGE_RUN_TEXT     "Ejecutar ${APP_NAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "English"

; -- .onInit -- ask all users vs current user ---------------------------------
Function .onInit
    ReadEnvStr $ProgramData PROGRAMDATA

    ; Silent mode (auto-update): detect existing install location from registry
    IfSilent silentDetect

    MessageBox MB_YESNO|MB_ICONQUESTION \
        "Instalar ${APP_NAME} para TODOS los usuarios?$\n$\n\
Si  -> Program Files  (configuracion compartida en ProgramData)$\n\
No  -> Solo esta cuenta (AppData del usuario actual)" \
        IDYES allUsers

    ; Current user
    StrCpy $AllUsers "0"
    StrCpy $INSTDIR "$LOCALAPPDATA\${APP_NAME}"
    Goto done

    allUsers:
    StrCpy $AllUsers "1"
    StrCpy $INSTDIR "$PROGRAMFILES64\${APP_NAME}"
    Goto done

    silentDetect:
    ; Try HKLM (all-users install)
    ReadRegStr $INSTDIR HKLM "${REG_UNINST}" "InstallLocation"
    StrCmp $INSTDIR "" 0 silentHKLM
    ; Try HKCU (current-user install)
    ReadRegStr $INSTDIR HKCU "${REG_UNINST}" "InstallLocation"
    StrCmp $INSTDIR "" 0 silentHKCU
    ; Fallback: current user
    StrCpy $INSTDIR "$LOCALAPPDATA\${APP_NAME}"
    StrCpy $AllUsers "0"
    Goto done

    silentHKLM:
    StrCpy $AllUsers "1"
    Goto done

    silentHKCU:
    StrCpy $AllUsers "0"

    done:
FunctionEnd

; -- Install section ----------------------------------------------------------
Section "Instalar"

    ; Stop any running instance
    ExecWait 'taskkill /f /im "${EXE_NAME}"' $0

    SetOutPath "$INSTDIR"
    File "dist\${EXE_NAME}"

    ; -- Config directory & registry key --------------------------------------
    ${If} $AllUsers == "1"
        ; Shared config in ProgramData
        CreateDirectory "$ProgramData\${APP_NAME}"
        WriteRegStr HKLM "${REG_APP}" "ConfigPath" "$ProgramData\${APP_NAME}"
        SetShellVarContext all
    ${Else}
        ; Per-user config in AppData\Roaming
        CreateDirectory "$APPDATA\${APP_NAME}"
        WriteRegStr HKCU "${REG_APP}" "ConfigPath" "$APPDATA\${APP_NAME}"
        SetShellVarContext current
    ${EndIf}

    ; -- Start Menu shortcut --------------------------------------------------
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
                    "$INSTDIR\${EXE_NAME}" "" "$INSTDIR\${EXE_NAME}" 0
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\Desinstalar ${APP_NAME}.lnk" \
                    "$INSTDIR\Uninstall.exe"

    ; -- Scheduled task (elevated, no UAC on startup) -------------------------
    ExecWait 'schtasks /create /tn "${TASK_NAME}" /tr "\"$INSTDIR\${EXE_NAME}\"" /sc onlogon /rl highest /f'

    ; -- Uninstaller ----------------------------------------------------------
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; -- Add/Remove Programs --------------------------------------------------
    ${If} $AllUsers == "1"
        WriteRegStr   HKLM "${REG_UNINST}" "DisplayName"     "${APP_NAME}"
        WriteRegStr   HKLM "${REG_UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
        WriteRegStr   HKLM "${REG_UNINST}" "InstallLocation" "$INSTDIR"
        WriteRegStr   HKLM "${REG_UNINST}" "DisplayVersion"  "${APP_VERSION}"
        WriteRegStr   HKLM "${REG_UNINST}" "Publisher"       "${APP_NAME}"
        WriteRegStr   HKLM "${REG_UNINST}" "DisplayIcon"     "$INSTDIR\${EXE_NAME}"
        WriteRegDWORD HKLM "${REG_UNINST}" "NoModify"        1
        WriteRegDWORD HKLM "${REG_UNINST}" "NoRepair"        1
    ${Else}
        WriteRegStr   HKCU "${REG_UNINST}" "DisplayName"     "${APP_NAME}"
        WriteRegStr   HKCU "${REG_UNINST}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
        WriteRegStr   HKCU "${REG_UNINST}" "InstallLocation" "$INSTDIR"
        WriteRegStr   HKCU "${REG_UNINST}" "DisplayVersion"  "${APP_VERSION}"
        WriteRegStr   HKCU "${REG_UNINST}" "Publisher"       "${APP_NAME}"
        WriteRegStr   HKCU "${REG_UNINST}" "DisplayIcon"     "$INSTDIR\${EXE_NAME}"
    ${EndIf}

    ; Silent auto-update: relaunch the app automatically
    IfSilent 0 showMsg
    Exec '"$INSTDIR\${EXE_NAME}"'
    Goto endSection

    showMsg:
    MessageBox MB_ICONINFORMATION \
        "${APP_NAME} instalado correctamente.$\n$\n\
- Tarea programada creada: se inicia automaticamente con Windows sin UAC.$\n\
- Puede activarlo/desactivarlo desde el icono de la bandeja -> Ejecutar al inicio."

    endSection:
SectionEnd

; -- Uninstall section --------------------------------------------------------
Section "Uninstall"

    ; Stop the app
    ExecWait 'taskkill /f /im "${EXE_NAME}"'

    ; Delete scheduled task
    ExecWait 'schtasks /delete /tn "${TASK_NAME}" /f'

    ; Remove files
    Delete "$INSTDIR\${EXE_NAME}"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir  "$INSTDIR"

    ; Remove Start Menu (try both all-users and current-user)
    SetShellVarContext all
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Desinstalar ${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"
    SetShellVarContext current
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    Delete "$SMPROGRAMS\${APP_NAME}\Desinstalar ${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    ; Remove registry keys
    DeleteRegKey HKLM "${REG_UNINST}"
    DeleteRegKey HKCU "${REG_UNINST}"
    DeleteRegKey HKLM "${REG_APP}"
    DeleteRegKey HKCU "${REG_APP}"

    ; NOTE: config files in ProgramData / AppData are intentionally kept
    ; so the user does not lose their whitelist/blacklist/settings.

SectionEnd
