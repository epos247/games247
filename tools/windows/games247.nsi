; games247 Windows installer (NSIS).
;
; Per-user install: no administrator prompt, which matters for silent in-app updates on
; handhelds such as the ROG Ally and Legion Go.
;
; Built by the GitHub workflow:
;   makensis /DVERSION=4.0.0-beta.1 /DSTAGEDIR=stage /DOUTFILE=games247-4.0.0-beta.1-setup.exe
;            /DICON=es-app\assets\games247.ico tools\windows\games247.nsi
;
; Silent install (used by the in-app updater): games247-...-setup.exe /S
; After a silent install, games247 is started again automatically.

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "Pass /DVERSION=<version>"
!endif
!ifndef STAGEDIR
  !error "Pass /DSTAGEDIR=<folder with the installed files>"
!endif
!ifndef OUTFILE
  !define OUTFILE "games247-${VERSION}-setup.exe"
!endif

!define APPNAME "games247"
!define EXENAME "games247.exe"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\games247"

Name "${APPNAME}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\${APPNAME}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
BrandingText "${APPNAME} ${VERSION}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Start games247"

!insertmacro MUI_PAGE_LICENSE "${STAGEDIR}\LICENSE"
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "0.0.0.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT license"

; Waits up to 20 seconds for a running games247 to close (the in-app updater quits games247
; right after starting this installer), so its files can be replaced.
!macro WaitForGames247
  StrCpy $1 0
  wait_loop:
    FindWindow $0 "SDL_app" "games247"
    StrCmp $0 0 wait_done
    IntOp $1 $1 + 1
    IntCmp $1 80 wait_done
    Sleep 250
    Goto wait_loop
  wait_done:
!macroend

Section "games247" SecMain
  !insertmacro WaitForGames247

  ; Remove the old program files first so files dropped in a newer version don't linger.
  ; User data (settings, saves, cores) lives in %USERPROFILE%\.games247 and isn't touched.
  RMDir /r "$INSTDIR\resources"
  RMDir /r "$INSTDIR\themes"

  SetOutPath "$INSTDIR"
  File /r "${STAGEDIR}\*.*"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortCut "$SMPROGRAMS\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
  IfSilent +2
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"

  ; Entry in Settings > Apps > Installed apps.
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\${EXENAME}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/epos247/games247"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" "$0"

  ; A silent install is an in-app update: start games247 again.
  IfSilent 0 +2
    Exec '"$INSTDIR\${EXENAME}"'
SectionEnd

Section "Uninstall"
  !insertmacro WaitForGames247
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  ; %USERPROFILE%\.games247 (settings, saves, downloaded cores) is kept on purpose.
SectionEnd
