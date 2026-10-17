!define APP_NAME "OceanTerm"
!define APP_PUBLISHER "OceanTerm"
!define APP_EXE "OceanTerm.exe"

; Overridable via makensis /D switches, e.g.:
;   makensis /DAPP_ARCH=x64 /DAPP_VERSION=1.0.3 /DSTAGING_DIR=package_staging_x64 package_windows.nsi
!ifndef APP_ARCH
    !define APP_ARCH "arm64"
!endif
!ifndef APP_VERSION
    !define APP_VERSION "1.0.3"
!endif
!ifndef STAGING_DIR
    !define STAGING_DIR "package_staging_${APP_ARCH}"
!endif

!if "${APP_ARCH}" == "x86"
    !define INSTALL_DIR "$PROGRAMFILES\${APP_NAME}"
!else
    !define INSTALL_DIR "$PROGRAMFILES64\${APP_NAME}"
!endif
!define STARTMENU_DIR "$SMPROGRAMS\${APP_NAME}"

; Request admin rights
RequestExecutionLevel admin

; Unicode installer (required to parse the UTF-8 SimpChinese strings below)
Unicode true

; Modern UI
!include "MUI2.nsh"

; General
Name "${APP_NAME}"
OutFile "OceanTerm-win-${APP_ARCH}-v${APP_VERSION}-setup.exe"
InstallDir "${INSTALL_DIR}"
InstallDirRegKey HKLM "Software\${APP_NAME}" "InstallLocation"
ShowInstDetails show
ShowUnInstDetails show

; Interface Settings
!define MUI_ABORTWARNING
!define MUI_ICON "src\OceanTerm.ico"
!define MUI_UNICON "src\OceanTerm.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
;!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

; Auto-detect system language
!define MUI_LANGDLL_WINDOWTITLE "Installer Language"
!define MUI_LANGDLL_INFO "Please select the language of the installer"

Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

; Installer Sections
Section "Main Application" SecMain
    SectionIn RO
    
    SetOutPath "$INSTDIR"
    
    ; All payload files (exe + DLLs + locales\) staged per-arch by build_installer.ps1
    File /r "${STAGING_DIR}\*"
    
    ; Create uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Register in Add/Remove Programs
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "DisplayVersion" "${APP_VERSION}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "Publisher" "${APP_PUBLISHER}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "InstallLocation" "$INSTDIR"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}" "NoRepair" 1
    
    ; Create start menu shortcuts
    CreateDirectory "${STARTMENU_DIR}"
    CreateShortCut "${STARTMENU_DIR}\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\OceanTerm.ico" 0
    CreateShortCut "${STARTMENU_DIR}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\OceanTerm.ico" 0
SectionEnd

; Section Descriptions
LangString DESC_SecMain ${LANG_ENGLISH} "The main application files and required libraries."
LangString DESC_SecMain ${LANG_SIMPCHINESE} "主程序文件及所需运行库。"
LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a desktop shortcut."
LangString DESC_SecDesktop ${LANG_SIMPCHINESE} "在桌面创建快捷方式。"

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; Uninstaller Section
Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\${APP_EXE}"
    Delete "$INSTDIR\*.dll"
    Delete "$INSTDIR\Uninstall.exe"
    
    ; Remove directories
    RMDir /r "$INSTDIR\locales"
    RMDir "$INSTDIR"
    
    ; Remove shortcuts
    Delete "${STARTMENU_DIR}\${APP_NAME}.lnk"
    Delete "${STARTMENU_DIR}\Uninstall.lnk"
    RMDir "${STARTMENU_DIR}"
    Delete "$DESKTOP\${APP_NAME}.lnk"
    
    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"
    DeleteRegKey HKLM "Software\${APP_NAME}"
SectionEnd
