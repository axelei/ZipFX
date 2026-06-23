@echo off
REM Build and package ZipFX for Windows
REM Requires: Qt 6.x MinGW, CMake, MinGW toolchain
REM Optional: NSIS (makensis.exe) for installer
REM
REM Usage:  build_win.cmd [Qt_DIR]
REM         default Qt_DIR = C:\Qt\6.8.3\mingw_64

setlocal

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=C:\Qt\6.8.3\mingw_64

set MINGW_DIR=%QT_DIR%\..\..\Tools\mingw1310_64\bin
set PATH=%QT_DIR%\bin;%MINGW_DIR%;%PATH%

set BUILD_DIR=build_win

echo === Configuring %BUILD_DIR% ===
cmake -S .. -B %BUILD_DIR% -G "MinGW Makefiles" ^
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
windeployqt --no-translations --no-compiler-runtime %BUILD_DIR%/ZipFX.exe
if errorlevel 1 echo Warning: windeployqt had issues

echo === Copying 7z.dll ===
xcopy /y /d ..\lib\win\x64\7z.dll %BUILD_DIR%\ 2>nul

echo === Checking NSIS ===
where makensis >nul 2>nul
if errorlevel 1 (
    echo NSIS not found. Downloading and installing...
    powershell -Command "$url = ((Invoke-RestMethod 'https://api.github.com/repos/kichik/nsis/releases/latest').assets | Where-Object { $_.name -like '*-setup.exe' } | Select-Object -First 1).browser_download_url; if ($url) { Invoke-WebRequest -Uri $url -OutFile '%TEMP%\nsis-setup.exe' -UseBasicParsing; Start-Process -Wait '%TEMP%\nsis-setup.exe' -ArgumentList '/S' } else { Write-Error 'NSIS release not found' }"
    if errorlevel 1 (
        echo Warning: Failed to install NSIS, creating zip package instead
        goto :makezip
    )
    rem Refresh PATH so makensis is found
    set PATH=%PATH%;%ProgramFiles(x86)%\NSIS;%ProgramFiles%\NSIS
)

echo === Building NSIS installer ===
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
