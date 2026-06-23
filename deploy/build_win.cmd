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

echo === Running windeployqt ===
where windeployqt >nul 2>nul
if errorlevel 1 (
    rem Try to find windeployqt near the Qt6_DIR that cmake used
    for /f "tokens=*" %%a in ('dir /s /b "%QT_DIR%\..\windeployqt.exe" 2^>nul') do set "WINDEPLOYQT=%%a"
    if "!WINDEPLOYQT!"=="" for /f "tokens=*" %%a in ('dir /s /b "%QT_DIR%\..\..\windeployqt.exe" 2^>nul') do set "WINDEPLOYQT=%%a"
    if "!WINDEPLOYQT!"=="" for /f "tokens=*" %%a in ('where windeployqt 2^>nul') do set "WINDEPLOYQT=%%a"
) else (
    set "WINDEPLOYQT=windeployqt"
)
if not "!WINDEPLOYQT!"=="" (
    "!WINDEPLOYQT!" --no-translations --no-compiler-runtime "%BUILD_DIR%/ZipFX.exe"
) else (
    echo Warning: windeployqt not found. Copy Qt DLLs manually.
)

echo === Copying 7z.dll ===
if exist "%REPO_DIR%\lib\win\x64\7z.dll" (
    copy /y "%REPO_DIR%\lib\win\x64\7z.dll" "%BUILD_DIR%\" >nul
    echo 7z.dll copied
) else (
    echo Warning: 7z.dll not found at %REPO_DIR%\lib\win\x64\7z.dll
)

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
powershell -Command "try { Invoke-WebRequest -Uri 'https://sourceforge.net/projects/nsis/files/NSIS%203/3.09/nsis-3.09-setup.exe/download' -OutFile '%TEMP%\nsis-setup.exe' -UseBasicParsing } catch { Write-Host 'Download failed' } ; if (Test-Path '%TEMP%\nsis-setup.exe') { Start-Process -Wait '%TEMP%\nsis-setup.exe' -ArgumentList '/S' } else { Write-Host 'NSIS installer not found' }"
if errorlevel 1 (
    echo Warning: Failed to install NSIS, creating zip package instead
    goto :makezip
)
rem Refresh PATH to find newly installed makensis
set "PATH=%PATH%;%ProgramFiles%\NSIS;%ProgramFiles(x86)%\NSIS"
where makensis >nul 2>nul || (
    echo Warning: NSIS installed but makensis.exe not found
    goto :makezip
)
makensis installer.nsi
if errorlevel 1 (
    echo Warning: NSIS installer failed
    goto :makezip
)
echo === Done: ZipFX-Setup.exe ===
goto :eof

:makezip
set PKG_NAME=ZipFX-win64.zip
powershell Compress-Archive -Path "%BUILD_DIR%\ZipFX.exe",^
    "%BUILD_DIR%\Qt6*.dll",^
    "%BUILD_DIR%\libzip.dll",^
    "%BUILD_DIR%\libadf.dll",^
    "%BUILD_DIR%\7z.dll",^
    "%BUILD_DIR%\platforms",^
    "%BUILD_DIR%\styles",^
    "%BUILD_DIR%\translations" ^
    -DestinationPath "%PKG_NAME%" -Force
echo === Done: %PKG_NAME% ===
