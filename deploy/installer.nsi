; ZipFX NSIS installer
; Requires: NSIS (https://nsis.sourceforge.io)
; Build with: makensis /DVERSION=0.2.0 installer.nsi

Unicode True

!ifndef VERSION
!define VERSION "0.2.0"
!endif

!include "MUI2.nsh"

Name "ZipFX"
OutFile "ZipFX-Setup.exe"
InstallDir "$PROGRAMFILES64\ZipFX"
InstallDirRegKey HKLM "Software\ZipFX" ""

; Request application privileges for Windows Vista+
RequestExecutionLevel admin

; Interface Settings
!define MUI_ABORTWARNING
!define MUI_ICON "..\src\resources\AppIcon.ico"
!define MUI_UNICON "..\src\resources\AppIcon.ico"

; Pages (wizard order)
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Languages
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"

    ; Main executable
    File "build_win\ZipFX.exe"

    ; Qt DLLs
    File "build_win\Qt6Core.dll"
    File "build_win\Qt6Gui.dll"
    File "build_win\Qt6Network.dll"
    File "build_win\Qt6Svg.dll"
    File "build_win\Qt6Widgets.dll"

    ; Third-party DLLs
    File "build_win\libzip.dll"
    File "build_win\libadf.dll"
    File "build_win\libStormLib.dll"
    File "build_win\7z.dll"
    File /nonfatal "build_win\ZipFXShellExt.dll"

    ; Qt plugins
    SetOutPath "$INSTDIR\platforms"
    File "build_win\platforms\qwindows.dll"

    SetOutPath "$INSTDIR\styles"
    File "build_win\styles\qmodernwindowsstyle.dll"

    SetOutPath "$INSTDIR\imageformats"
    File /nonfatal "build_win\imageformats\*.dll"
    SetOutPath "$INSTDIR\iconengines"
    File /nonfatal "build_win\iconengines\*.dll"
    SetOutPath "$INSTDIR\tls"
    File /nonfatal "build_win\tls\*.dll"
    SetOutPath "$INSTDIR\networkinformation"
    File /nonfatal "build_win\networkinformation\*.dll"
    SetOutPath "$INSTDIR\generic"
    File /nonfatal "build_win\generic\*.dll"
    SetOutPath "$INSTDIR"
    File /nonfatal "build_win\opengl32sw.dll"
    File /nonfatal "build_win\D3Dcompiler_47.dll"

    ; Translations
    SetOutPath "$INSTDIR\translations"
    File /r "build_win\translations\*.qm"
    SetOutPath "$INSTDIR"

    ; App icon for shortcuts
    File "..\src\resources\AppIcon.ico"

    ; License
    File "..\LICENSE"

    ; Start menu
    CreateDirectory "$SMPROGRAMS\ZipFX"
    CreateShortCut "$SMPROGRAMS\ZipFX\ZipFX.lnk" "$INSTDIR\ZipFX.exe" "" "$INSTDIR\AppIcon.ico"
    CreateShortCut "$SMPROGRAMS\ZipFX\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\AppIcon.ico"

    ; Desktop shortcut
    CreateShortCut "$DESKTOP\ZipFX.lnk" "$INSTDIR\ZipFX.exe" "" "$INSTDIR\AppIcon.ico"

    ; File associations
    WriteRegStr HKCR ".zip"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".7z"    "" "ZipFX.Archive"
    WriteRegStr HKCR ".rar"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".tar"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".gz"    "" "ZipFX.Archive"
    WriteRegStr HKCR ".tgz"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".cab"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".iso"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".lzh"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".xar"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".cpio"  "" "ZipFX.Archive"
    WriteRegStr HKCR ".wad"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".pak"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".grp"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".hog"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".vpk"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".mpq"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".mpk"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".w3x"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".w3m"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".adf"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".adz"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".ima"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".img"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".flp"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".dsk"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".lha"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".vhd"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".vhdx"  "" "ZipFX.Archive"
    WriteRegStr HKCR ".vmdk"  "" "ZipFX.Archive"
    WriteRegStr HKCR ".qcow"  "" "ZipFX.Archive"
    WriteRegStr HKCR ".qcow2" "" "ZipFX.Archive"
    WriteRegStr HKCR ".nrg"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".bin"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".cue"   "" "ZipFX.Archive"
    WriteRegStr HKCR ".a"     "" "ZipFX.Archive"
    WriteRegStr HKCR ".deb"   "" "ZipFX.Archive"

    WriteRegStr HKCR "ZipFX.Archive" "" "ZipFX Archive"
    WriteRegStr HKCR "ZipFX.Archive\DefaultIcon" "" "$INSTDIR\AppIcon.ico"
    WriteRegStr HKCR "ZipFX.Archive\shell\open\command" "" '"$INSTDIR\ZipFX.exe" "%1"'

    ; Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "DisplayName" "ZipFX"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "DisplayIcon" "$INSTDIR\AppIcon.ico"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "Publisher" "ZipFX Team"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "DisplayVersion" "${VERSION}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "EstimatedSize" 200000
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "URLInfoAbout" "https://github.com/axelei/ZipFX"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX" \
        "NoRepair" 1
SectionEnd

Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\ZipFX.exe"
    Delete "$INSTDIR\Qt6Core.dll"
    Delete "$INSTDIR\Qt6Gui.dll"
    Delete "$INSTDIR\Qt6Network.dll"
    Delete "$INSTDIR\Qt6Svg.dll"
    Delete "$INSTDIR\Qt6Widgets.dll"
    Delete "$INSTDIR\libzip.dll"
    Delete "$INSTDIR\libadf.dll"
    Delete "$INSTDIR\libStormLib.dll"
    Delete "$INSTDIR\7z.dll"
    Delete "$INSTDIR\ZipFXShellExt.dll"
    Delete "$INSTDIR\AppIcon.ico"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\iconengines"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\generic"
    RMDir /r "$INSTDIR\translations"
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\ZipFX\ZipFX.lnk"
    Delete "$SMPROGRAMS\ZipFX\Uninstall.lnk"
    RMDir "$SMPROGRAMS\ZipFX"
    Delete "$DESKTOP\ZipFX.lnk"

    ; Remove file associations — DeleteRegKey wipes all sub-keys
    ; (DefaultIcon, shell\open\command) registered under the ProgID.
    DeleteRegKey HKCR "ZipFX.Archive"

    ; Remove uninstall entry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX"
    DeleteRegKey HKLM "Software\ZipFX"
SectionEnd
