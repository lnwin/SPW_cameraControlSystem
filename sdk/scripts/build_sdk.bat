@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: build_sdk.bat  --  one-click SDK build for Windows x64 MSVC
::
:: Prerequisites:
::   - Visual Studio 2022 (or 2019) with C++ workload installed
::   - Qt6, GStreamer, FFmpeg paths configured below
:: ============================================================

:: ---------------- User-configurable paths ----------------------------
set QT_ROOT=C:\Qt\6.4.3\msvc2019_64
set GST_ROOT=E:\ThirdParty\Gstreamer\1.0\msvc_x86_64
set FFMPEG_ROOT=E:\ThirdParty\ffmpeg-8.0-full_build-shared

:: Visual Studio version: "Visual Studio 17 2022" or "Visual Studio 16 2019"
set VS_GENERATOR=Visual Studio 17 2022

:: Build type: Release or Debug
set BUILD_TYPE=Release

:: Number of parallel jobs (0 = use all cores)
set JOBS=0
:: ---------------------------------------------------------------------

set SCRIPT_DIR=%~dp0
set SDK_DIR=%SCRIPT_DIR%..
set BUILD_DIR=%SDK_DIR%\build

echo.
echo ====================================================
echo  SPWater Camera SDK  --  Build Script
echo  Build type : %BUILD_TYPE%
echo  Qt root    : %QT_ROOT%
echo  GST root   : %GST_ROOT%
echo  FFmpeg root: %FFMPEG_ROOT%
echo ====================================================
echo.

:: Check CMake
cmake --version >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found on PATH.
    echo         Install CMake 3.22+ and add it to PATH.
    exit /b 1
)

:: Find VS Dev Shell
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% (
    for /f "usebackq tokens=*" %%i in (
        `%VSWHERE% -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
    ) do set VS_INSTALL=%%i
)

if not defined VS_INSTALL (
    echo [ERROR] Visual Studio installation not found.
    exit /b 1
)

:: Initialise VS environment
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

:: Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

:: Configure
echo [1/3] Configuring...
cmake "%SDK_DIR%" ^
    -G "%VS_GENERATOR%" -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DQT_ROOT="%QT_ROOT%" ^
    -DGST_ROOT="%GST_ROOT%" ^
    -DFFMPEG_ROOT="%FFMPEG_ROOT%" ^
    -DSPWCAM_BUILD_EXAMPLES=ON ^
    -DSPWCAM_BUILD_TESTS=ON

if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

:: Build
echo [2/3] Building (%BUILD_TYPE%)...
if %JOBS%==0 (
    cmake --build . --config %BUILD_TYPE% --parallel
) else (
    cmake --build . --config %BUILD_TYPE% --parallel %JOBS%
)

if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

:: Run tests
:: The in-tree test executables load the third-party DLLs from their install
:: directories (the self-contained bin\ is only assembled by package_sdk.bat).
:: Set SPWCAM_TEST_RTSP_URL=rtsp://... before running to enable the end-to-end
:: stream/record/snapshot test; without it that test is skipped.
echo [3/3] Running tests...
set PATH=%QT_ROOT%\bin;%GST_ROOT%\bin;%FFMPEG_ROOT%\bin;%PATH%
ctest -C %BUILD_TYPE% --output-on-failure

if errorlevel 1 (
    echo [ERROR] One or more tests failed.  Check output above.
    exit /b 1
) else (
    echo [OK] All tests passed.
)

echo.
echo ====================================================
echo  Build complete.
echo  Outputs in: %BUILD_DIR%\bin\%BUILD_TYPE%\  (import lib: %BUILD_DIR%\lib\%BUILD_TYPE%\)
echo  Next step : scripts\package_sdk.bat
echo ====================================================
echo.

endlocal
