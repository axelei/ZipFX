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

echo === Building NSIS installer ===
where makensis >nul 2>nul
if not errorlevel 1 (
    makensis installer.nsi
    if errorlevel 1 (
        echo Warning: NSIS installer failed
    ) else (
        echo === Done: ZipFX-Setup.exe ===
    )
) else (
    echo === NSIS not found, creating zip package instead ===
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
)
