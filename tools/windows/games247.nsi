; games247 Windows installer (NSIS).
;
; Per-user install: no administrator prompt, which matters for silent in-app updates on
; handhelds such as the ROG Ally and Legion Go.
;
; Built by the GitHub workflow:
;   makensis /DVERSION=4.0.0-beta.1 /DSTAGEDIR=stage /DOUTFILE=games247-4.0.0-beta.1-setup.exe
;            /DICON=es-app\assets\games247.ico tools\windows\games247.nsi
;
; In-app updates: games247 starts the installer with /S (or /UPDATE) and quits. The installer
; then skips the wizard and shows a full-screen splash with a progress bar instead, and starts
; games247 again when it's done.

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
!include "LogicLib.nsh"
!include "WinMessages.nsh"

!ifdef ICON
  !define MUI_ICON "${ICON}"
  !define MUI_UNICON "${ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXENAME}"
!define MUI_FINISHPAGE_RUN_TEXT "Start games247"

!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageWhenUpdating
!insertmacro MUI_PAGE_LICENSE "${STAGEDIR}\LICENSE"
!define MUI_PAGE_CUSTOMFUNCTION_SHOW ShowUpdateSplash
!insertmacro MUI_PAGE_INSTFILES
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipPageWhenUpdating
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

; 1 when the in-app updater started this installer.
Var UpdateMode
Var SplashTitleFont
Var SplashTextFont

Function .onInit
  StrCpy $UpdateMode 0
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $UpdateMode 1
  ${EndIf}
  ; games247 4.0.0-beta.1 and beta.2 start updates with /S. Show the splash for those too.
  ${If} ${Silent}
    StrCpy $UpdateMode 1
    SetSilent normal
  ${EndIf}
  ${If} $UpdateMode == 1
    SetAutoClose true
    InitPluginsDir
    File "/oname=$PLUGINSDIR\splash.bmp" "update_splash.bmp"
  ${EndIf}
FunctionEnd

Function SkipPageWhenUpdating
  ${If} $UpdateMode == 1
    Abort
  ${EndIf}
FunctionEnd

; Turns the progress page into a full-screen splash: black background, the games247 logo,
; a title and an orange progress bar, covering the whole screen like the app itself.
Function ShowUpdateSplash
  ${If} $UpdateMode != 1
    Return
  ${EndIf}

  System::Call 'user32::GetSystemMetrics(i 0) i .r6'
  System::Call 'user32::GetSystemMetrics(i 1) i .r7'

  ; Borderless and always on top (WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN).
  System::Call 'user32::SetWindowLongW(p $HWNDPARENT, i -16, i 0x96000000)'
  System::Call 'user32::SetWindowPos(p $HWNDPARENT, p -1, i 0, i 0, i r6, i r7, i 0x60)'
  SetCtlColors $HWNDPARENT FFFFFF 000000

  ; Hide the wizard's buttons, header and branding, keeping only the page itself.
  FindWindow $8 "#32770" "" $HWNDPARENT
  System::Call 'user32::GetWindow(p $HWNDPARENT, i 5) p .r0'
  ${While} $0 != 0
    ${If} $0 != $8
      ShowWindow $0 ${SW_HIDE}
    ${EndIf}
    System::Call 'user32::GetWindow(p r0, i 2) p .r0'
  ${EndWhile}

  ; The page fills the screen. Hide its status line, details list and details button.
  System::Call 'user32::SetWindowPos(p r8, p 0, i 0, i 0, i r6, i r7, i 0x4)'
  SetCtlColors $8 FFFFFF 000000
  GetDlgItem $0 $8 1006
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $8 1016
  ShowWindow $0 ${SW_HIDE}
  GetDlgItem $0 $8 1027
  ShowWindow $0 ${SW_HIDE}

  ; Logo (SS_BITMAP | SS_CENTERIMAGE), centred above the middle of the screen.
  IntOp $1 $6 - 192
  IntOp $1 $1 / 2
  IntOp $2 $7 / 2
  IntOp $2 $2 - 220
  System::Call 'user32::CreateWindowExW(i 0, w "STATIC", w "", i 0x5000020E, i r1, i r2, i 192, i 192, p r8, p 0, p 0, p 0) p .r3'
  System::Call 'user32::LoadImageW(p 0, w "$PLUGINSDIR\splash.bmp", i 0, i 192, i 192, i 0x10) p .r4'
  SendMessage $3 0x172 0 $4

  ; Title (SS_CENTER).
  IntOp $2 $7 / 2
  System::Call 'user32::CreateWindowExW(i 0, w "STATIC", w "Updating games247 to ${VERSION}", i 0x50000001, i 0, i r2, i r6, i 50, p r8, p 0, p 0, p 0) p .r3'
  CreateFont $SplashTitleFont "Segoe UI" 22 600
  SendMessage $3 ${WM_SETFONT} $SplashTitleFont 1
  SetCtlColors $3 FFFFFF transparent

  ; Progress bar: flat, orange on dark grey, half the screen wide.
  GetDlgItem $3 $8 1004
  System::Call 'uxtheme::SetWindowTheme(p r3, w " ", w " ")'
  System::Call 'user32::GetWindowLongW(p r3, i -16) i .r0'
  IntOp $0 $0 | 0x1
  System::Call 'user32::SetWindowLongW(p r3, i -16, i r0)'
  System::Call 'user32::SetWindowLongW(p r3, i -20, i 0)'
  IntOp $1 $6 / 4
  IntOp $4 $6 / 2
  IntOp $2 $7 / 2
  IntOp $2 $2 + 75
  System::Call 'user32::SetWindowPos(p r3, p 0, i r1, i r2, i r4, i 10, i 0x24)'
  SendMessage $3 0x409 0 0x001A7AFF
  SendMessage $3 0x2001 0 0x00333333

  ; Small note under the bar.
  IntOp $2 $2 + 30
  System::Call 'user32::CreateWindowExW(i 0, w "STATIC", w "games247 restarts by itself when the update is done", i 0x50000001, i 0, i r2, i r6, i 30, p r8, p 0, p 0, p 0) p .r3'
  CreateFont $SplashTextFont "Segoe UI" 11 400
  SendMessage $3 ${WM_SETFONT} $SplashTextFont 1
  SetCtlColors $3 A0A0A0 transparent
FunctionEnd

; An update starts games247 again.
Function .onInstSuccess
  ${If} $UpdateMode == 1
    Exec '"$INSTDIR\${EXENAME}"'
  ${EndIf}
FunctionEnd

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
  ; Only a first install adds a desktop shortcut, so updates don't bring back a deleted one.
  ${If} $UpdateMode != 1
    CreateShortCut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${EXENAME}"
  ${EndIf}

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
SectionEnd

Section "Uninstall"
  !insertmacro WaitForGames247
  Delete "$SMPROGRAMS\${APPNAME}.lnk"
  Delete "$DESKTOP\${APPNAME}.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  ; %USERPROFILE%\.games247 (settings, saves, downloaded cores) is kept on purpose.
SectionEnd
