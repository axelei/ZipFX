; ZipFX NSIS installer
; Requires: NSIS (https://nsis.sourceforge.io)
; Build with: makensis installer.nsi

Unicode True
Name "ZipFX"
OutFile "ZipFX-Setup.exe"
InstallDir "$PROGRAMFILES64\ZipFX"
RequestExecutionLevel admin

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
    File "build_win\7z.dll"

    ; Qt platform plugin
    CreateDirectory "$INSTDIR\platforms"
    File "build_win\platforms\qwindows.dll"

    ; Qt styles
    CreateDirectory "$INSTDIR\styles"
    File "build_win\styles\qmodernwindowsstyle.dll"

    ; Translations
    CreateDirectory "$INSTDIR\translations"
    File /r "build_win\translations\*.qm"

    ; App icon for desktop shortcut
    File "..\src\resources\AppIcon.ico"

    ; Start menu
    CreateDirectory "$SMPROGRAMS\ZipFX"
    CreateShortCut "$SMPROGRAMS\ZipFX\ZipFX.lnk" "$INSTDIR\ZipFX.exe" "" "$INSTDIR\AppIcon.ico"

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
    WriteRegStr HKCR ".adf"   "" "ZipFX.Archive"

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
    Delete "$INSTDIR\7z.dll"
    Delete "$INSTDIR\AppIcon.ico"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\translations"
    RMDir "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\ZipFX\ZipFX.lnk"
    RMDir "$SMPROGRAMS\ZipFX"
    Delete "$DESKTOP\ZipFX.lnk"

    ; Remove file associations
    DeleteRegKey HKCR "ZipFX.Archive"
    DeleteRegValue HKCR ".zip" ""
    DeleteRegValue HKCR ".7z" ""
    DeleteRegValue HKCR ".rar" ""
    DeleteRegValue HKCR ".tar" ""
    DeleteRegValue HKCR ".gz" ""
    DeleteRegValue HKCR ".tgz" ""
    DeleteRegValue HKCR ".cab" ""
    DeleteRegValue HKCR ".iso" ""
    DeleteRegValue HKCR ".lzh" ""
    DeleteRegValue HKCR ".xar" ""
    DeleteRegValue HKCR ".cpio" ""
    DeleteRegValue HKCR ".wad" ""
    DeleteRegValue HKCR ".pak" ""
    DeleteRegValue HKCR ".grp" ""
    DeleteRegValue HKCR ".hog" ""
    DeleteRegValue HKCR ".vpk" ""
    DeleteRegValue HKCR ".adf" ""

    ; Remove uninstall entry
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\ZipFX"
SectionEnd
