@echo off
REM Build and package ZipFX for Windows
REM Requires: Qt 6.x MinGW, CMake, MinGW toolchain
REM Optional: NSIS (makensis.exe) for installer
REM
REM Usage:  build_win.cmd [Qt_DIR]
REM         default Qt_DIR = C:\Qt\6.8.3\mingw_64

setlocal EnableDelayedExpansion

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.8.3\mingw_64

set MINGW_DIR=%QT_DIR%\..\..\Tools\mingw1310_64\bin
set PATH=%QT_DIR%\bin;%MINGW_DIR%;%PATH%

set BUILD_DIR=build_win
set REPO_DIR=..

echo === Configuring %BUILD_DIR% ===
cmake -S %REPO_DIR% -B %BUILD_DIR% -G "MinGW Makefiles" ^
    -DCMAKE_PREFIX_PATH=%QT_DIR% ^
    -DCMAKE_C_COMPILER=gcc ^
    -DCMAKE_CXX_COMPILER=g++ ^
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
    -DBUILD_TESTING=OFF

if errorlevel 1 exit /b 1

echo === Building ===
cmake --build %BUILD_DIR% --target ZipFX -j8
if errorlevel 1 exit /b 1

echo === Deploying Qt DLLs ===
set WINDEPLOYQT=
where windeployqt >nul 2>nul && set "WINDEPLOYQT=windeployqt"
if "!WINDEPLOYQT!"=="" for /f "tokens=*" %%a in ('dir /s /b "%QT_DIR%\..\windeployqt.exe" 2^>nul') do set "WINDEPLOYQT=%%a"
if "!WINDEPLOYQT!"=="" for /f "tokens=*" %%a in ('dir /s /b "%QT_DIR%\..\..\windeployqt.exe" 2^>nul') do set "WINDEPLOYQT=%%a"
if not "!WINDEPLOYQT!"=="" (
    echo Running windeployqt...
    "!WINDEPLOYQT!" --no-translations --no-compiler-runtime "%BUILD_DIR%/ZipFX.exe"
) else (
    echo windeployqt not found. Copying Qt DLLs manually...
    if exist "%QT_DIR%\bin\Qt6Core.dll" copy /y "%QT_DIR%\bin\Qt6Core.dll" "%BUILD_DIR%\" >nul
    if exist "%QT_DIR%\bin\Qt6Gui.dll" copy /y "%QT_DIR%\bin\Qt6Gui.dll" "%BUILD_DIR%\" >nul
    if exist "%QT_DIR%\bin\Qt6Widgets.dll" copy /y "%QT_DIR%\bin\Qt6Widgets.dll" "%BUILD_DIR%\" >nul
    if exist "%QT_DIR%\bin\Qt6Network.dll" copy /y "%QT_DIR%\bin\Qt6Network.dll" "%BUILD_DIR%\" >nul
    if exist "%QT_DIR%\bin\Qt6Svg.dll" copy /y "%QT_DIR%\bin\Qt6Svg.dll" "%BUILD_DIR%\" >nul
    mkdir "%BUILD_DIR%\platforms" 2>nul
    if exist "%QT_DIR%\plugins\platforms\qwindows.dll" copy /y "%QT_DIR%\plugins\platforms\qwindows.dll" "%BUILD_DIR%\platforms\" >nul
    if exist "%QT_DIR%\..\..\plugins\platforms\qwindows.dll" copy /y "%QT_DIR%\..\..\plugins\platforms\qwindows.dll" "%BUILD_DIR%\platforms\" >nul
)

if not exist "%BUILD_DIR%\platforms\qwindows.dll" echo ERROR: Qt platform plugin (qwindows.dll) missing

echo === Copying 7z.dll ===
if exist "%REPO_DIR%\lib\win\x64\7z.dll" (
    copy /y "%REPO_DIR%\lib\win\x64\7z.dll" "%BUILD_DIR%\" >nul
    echo 7z.dll copied
) else (
    echo Warning: 7z.dll not found at %REPO_DIR%\lib\win\x64\7z.dll
)

if not exist "%BUILD_DIR%\ZipFX.exe" echo WARNING: ZipFX.exe not found
if not exist "%BUILD_DIR%\platforms\qwindows.dll" echo WARNING: qwindows.dll not found
if not exist "%BUILD_DIR%\Qt6Core.dll" echo WARNING: Qt6Core.dll not found

echo === Checking NSIS ===
set "NSIS_DIR="
where makensis >nul 2>nul
if not errorlevel 1 (
    set "NSIS_DIR=makensis"
) else (
    if exist "%ProgramFiles%\NSIS\makensis.exe" set "NSIS_DIR=%ProgramFiles%\NSIS\makensis.exe"
    if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "NSIS_DIR=%ProgramFiles(x86)%\NSIS\makensis.exe"
)

if not "!NSIS_DIR!"=="" (
    echo === Building NSIS installer ===
    "!NSIS_DIR!" installer.nsi
    if errorlevel 1 (
        echo Warning: NSIS installer failed
        goto :makezip
    )
    echo === Done: ZipFX-Setup.exe ===
    goto :eof
)

rem If NSIS not found, try installing it
echo NSIS not found. Downloading and installing...
curl.exe -sL -o "%TEMP%\nsis-setup.exe" "https://sourceforge.net/projects/nsis/files/latest/download"
if not exist "%TEMP%\nsis-setup.exe" (
    echo Download via curl failed, trying PowerShell...
    powershell -Command "Invoke-WebRequest -Uri 'https://sourceforge.net/projects/nsis/files/NSIS%203/3.09/nsis-3.09-setup.exe/download' -OutFile '%TEMP%\nsis-setup.exe' -UseBasicParsing"
)
if not exist "%TEMP%\nsis-setup.exe" (
    echo Warning: Failed to download NSIS installer
    goto :makezip
)
echo Running NSIS installer...
"%TEMP%\nsis-setup.exe" /S
if not exist "%ProgramFiles%\NSIS\makensis.exe" if not exist "%ProgramFiles(x86)%\NSIS\makensis.exe" (
    echo Warning: NSIS installer ran but makensis.exe not found
    goto :makezip
)
echo === Building NSIS installer ===
set "PATH=%PATH%;%ProgramFiles%\NSIS;%ProgramFiles(x86)%\NSIS"
makensis /DVERSION=0.2.0 installer.nsi
if errorlevel 1 (
    echo Warning: NSIS installer build failed
    goto :makezip
)
echo === Done: ZipFX-Setup.exe ===
goto :eof

:makezip
set PKG_NAME=ZipFX-win64.zip
powershell Compress-Archive -Path "%BUILD_DIR%\ZipFX.exe",^
    "%BUILD_DIR%\Qt6*.dll",^
    "%BUILD_DIR%\7z.dll",^
    "%BUILD_DIR%\platforms",^
    "%BUILD_DIR%\styles",^
    "%BUILD_DIR%\translations" ^
    -DestinationPath "%PKG_NAME%" -Force
echo === Done: %PKG_NAME% ===
