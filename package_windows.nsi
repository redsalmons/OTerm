!define APP_NAME "OceanTerm"
!define APP_VERSION "1.0.0"
!define APP_PUBLISHER "OceanTerm"
!define APP_EXE "OceanTerm.exe"

!define INSTALL_DIR "$PROGRAMFILES64\${APP_NAME}"
!define STARTMENU_DIR "$SMPROGRAMS\${APP_NAME}"

; Request admin rights
RequestExecutionLevel admin

; Modern UI
!include "MUI2.nsh"

; General
Name "${APP_NAME}"
OutFile "OceanTerm-win-v${APP_VERSION}-setup.exe"
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
;!insertmacro MUI_LANGUAGE "SimpChinese"

; Installer Sections
Section "Main Application" SecMain
    SectionIn RO
    
    SetOutPath "$INSTDIR"
    
    ; Main executable
    File "build\bin\Release\${APP_EXE}"
    
    ; Required DLLs from vcpkg
    File "K:\vcpkg\installed\arm64-windows\bin\libcrypto-3-arm64.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libssl-3-arm64.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libssh2.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\uv.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\z.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\liblzma.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libexpat.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\pcre2-16.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libpng16.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\jpeg62.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\tiff.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libwebp.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libwebpdecoder.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\libwebpdemux.dll"
    
    ; Icon file
    File "src\OceanTerm.ico"
    
    ; wxWidgets DLLs
    File "K:\vcpkg\installed\arm64-windows\bin\wxbase331u_vc_x64_custom.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\wxmsw331u_core_vc_x64_custom.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\wxmsw331u_gl_vc_x64_custom.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\wxmsw331u_adv_vc_x64_custom.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\wxmsw331u_html_vc_x64_custom.dll"
    File "K:\vcpkg\installed\arm64-windows\bin\wxmsw331u_xrc_vc_x64_custom.dll"
    
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

Section "Internationalization Files" SecI18n
    SetOutPath "$INSTDIR\locales"
    File /r "locales\*"
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_EXE}" "" "$INSTDIR\OceanTerm.ico" 0
SectionEnd

; Section Descriptions
LangString DESC_SecMain ${LANG_ENGLISH} "The main application files and required libraries."
LangString DESC_SecI18n ${LANG_ENGLISH} "Internationalization files for multi-language support."
LangString DESC_SecDesktop ${LANG_ENGLISH} "Create a desktop shortcut."

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
    !insertmacro MUI_DESCRIPTION_TEXT ${SecI18n} $(DESC_SecI18n)
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
