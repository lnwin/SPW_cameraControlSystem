@echo off
setlocal EnableDelayedExpansion

:: ============================================================
:: package_sdk.bat  --  assemble the redistributable SDK package
::
:: Input : build\bin\Release\spwcam_sdk.dll   (from build_sdk.bat)
::         or build_ci\bin\Release\spwcam_sdk.dll (CI layout)
:: Output: dist\spwcam_sdk_v<VERSION>\  (+ .zip if PowerShell available)
::
:: The package is self-contained: every non-system DLL required by
:: spwcam_sdk.dll AND by every bundled GStreamer plugin is included.
:: A dependency-closure check (dumpbin) is run at the end and FAILS the
:: packaging if anything is missing.
:: ============================================================

set VERSION=2.0.0
set SDK=%~dp0..
set DIST=%SDK%\dist\spwcam_sdk_v%VERSION%

:: ---- locate build output (build\ preferred, build_ci\ fallback) ----
set BUILD_BIN=
set BUILD_LIB=
for %%d in (build build_ci) do (
    if not defined BUILD_BIN if exist "%SDK%\%%d\bin\Release\spwcam_sdk.dll" (
        set BUILD_BIN=%SDK%\%%d\bin\Release
        set BUILD_LIB=%SDK%\%%d\lib\Release
    )
)
if not defined BUILD_BIN (
    echo ERROR: spwcam_sdk.dll not found in build\bin\Release or build_ci\bin\Release.
    echo        Run scripts\build_sdk.bat first.
    exit /b 1
)

:: ---- third-party roots (must match build_sdk.bat) ----
set QT=C:\Qt\6.4.3\msvc2019_64
set GST=E:\ThirdParty\Gstreamer\1.0\msvc_x86_64
set FFMPEG=E:\ThirdParty\ffmpeg-8.0-full_build-shared
set CRT=
for /d %%v in ("C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\14.*") do (
    if exist "%%~v\x64\Microsoft.VC143.CRT\vcruntime140.dll" set CRT=%%~v\x64\Microsoft.VC143.CRT
)

echo -------------------------------------------------------
echo  SPWater Camera SDK  package  v%VERSION%
echo  Build : %BUILD_BIN%
echo  Output: %DIST%
echo -------------------------------------------------------

set FAIL=0

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%\bin\gst_plugins"
mkdir "%DIST%\lib"
mkdir "%DIST%\include\spwcam"
mkdir "%DIST%\python\spwcam"
mkdir "%DIST%\examples\c"
mkdir "%DIST%\examples\cpp"
mkdir "%DIST%\examples\python"
mkdir "%DIST%\docs"

echo [1] SDK DLL / import lib...
copy /y "%BUILD_BIN%\spwcam_sdk.dll" "%DIST%\bin\" >nul
if exist "%BUILD_LIB%\spwcam_sdk.lib" (copy /y "%BUILD_LIB%\spwcam_sdk.lib" "%DIST%\lib\" >nul) else (copy /y "%BUILD_BIN%\spwcam_sdk.lib" "%DIST%\lib\" >nul)
if not exist "%DIST%\lib\spwcam_sdk.lib" (echo     ERROR: import lib missing & set FAIL=1)
:: prebuilt examples: let the package be tried immediately after unzipping
for %%f in (example_c_basic.exe example_cpp_raii.exe) do (
    if exist "%BUILD_BIN%\%%f" copy /y "%BUILD_BIN%\%%f" "%DIST%\bin\" >nul
)

echo [2] Qt runtime (release only)...
for %%f in (Qt6Core.dll Qt6Network.dll Qt6Gui.dll) do (
    if exist "%QT%\bin\%%f" (copy /y "%QT%\bin\%%f" "%DIST%\bin\" >nul) else (echo     ERROR: %%f & set FAIL=1)
)
:: Qt plugins actually used by the SDK: image formats for snapshots (PNG/BMP are
:: built into Qt6Gui; JPG needs qjpeg).  No platform/tls/network plugins needed:
:: the SDK runs on a QCoreApplication and uses plain UDP.
mkdir "%DIST%\bin\imageformats"
if exist "%QT%\plugins\imageformats\qjpeg.dll" (copy /y "%QT%\plugins\imageformats\qjpeg.dll" "%DIST%\bin\imageformats\" >nul) else (echo     WARN: qjpeg.dll missing - JPG snapshots unavailable)

echo [3] FFmpeg runtime...
for %%f in (avcodec-62.dll avformat-62.dll avutil-60.dll swscale-9.dll swresample-6.dll) do (
    if exist "%FFMPEG%\bin\%%f" (copy /y "%FFMPEG%\bin\%%f" "%DIST%\bin\" >nul) else (echo     ERROR: %%f & set FAIL=1)
)

echo [4] GStreamer core + support libraries...
:: Every library below is a direct or transitive import of spwcam_sdk.dll or of
:: one of the plugins in step [5].  Verified with the closure check in step [9].
for %%f in (
  gstreamer-1.0-0.dll gstapp-1.0-0.dll gstbase-1.0-0.dll gstvideo-1.0-0.dll
  gstaudio-1.0-0.dll gsttag-1.0-0.dll gstpbutils-1.0-0.dll
  gstrtsp-1.0-0.dll gstsdp-1.0-0.dll gstrtp-1.0-0.dll gstnet-1.0-0.dll
  gstallocators-1.0-0.dll gstcontroller-1.0-0.dll
  gstcodecparsers-1.0-0.dll gstcodecs-1.0-0.dll
  gstd3d11-1.0-0.dll gstd3d12-1.0-0.dll gstd3dshader-1.0-0.dll gstdxva-1.0-0.dll
  gstcuda-1.0-0.dll gstgl-1.0-0.dll
  glib-2.0-0.dll gobject-2.0-0.dll gmodule-2.0-0.dll gio-2.0-0.dll
  ffi-7.dll orc-0.4-0.dll pcre2-8-0.dll z-1.dll intl-8.dll
) do (
    if exist "%GST%\bin\%%f" (copy /y "%GST%\bin\%%f" "%DIST%\bin\" >nul) else (echo     ERROR: %%f & set FAIL=1)
)

echo [5] GStreamer plugins (RTSP / H.264 decode)...
set GPLUG=%GST%\lib\gstreamer-1.0
for %%f in (
  gstcoreelements.dll gsttypefindfunctions.dll
  gstapp.dll gstrtsp.dll gstrtp.dll
  gstrtpmanager.dll gstrtpmanagerbad.dll gstrtponvif.dll
  gstudp.dll gsttcp.dll
  gstvideoconvertscale.dll gstvideoparsersbad.dll
  gstvideofilter.dll gstvideofiltersbad.dll
  gstplayback.dll gstopenh264.dll
  gstd3d11.dll gstd3d12.dll gstnvcodec.dll
) do (
    if exist "%GPLUG%\%%f" (copy /y "%GPLUG%\%%f" "%DIST%\bin\gst_plugins\" >nul) else (echo     ERROR: %%f & set FAIL=1)
)
for %%f in (openh264-7.dll) do (
    if exist "%GST%\bin\%%f" (copy /y "%GST%\bin\%%f" "%DIST%\bin\" >nul) else (echo     ERROR: %%f & set FAIL=1)
)

echo [6] MSVC CRT...
if defined CRT (
    for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll msvcp140_atomic_wait.dll) do (
        if exist "%CRT%\%%f" (copy /y "%CRT%\%%f" "%DIST%\bin\" >nul) else (echo     WARN: %%f)
    )
) else (
    echo     WARN: VC redist directory not found - CRT DLLs not bundled
)

echo [7] Headers / Python / examples / docs...
copy /y "%SDK%\include\spwcam\spwcam.h"   "%DIST%\include\spwcam\" >nul
copy /y "%SDK%\include\spwcam\spwcam.hpp" "%DIST%\include\spwcam\" >nul
copy /y "%SDK%\python\spwcam\*.py" "%DIST%\python\spwcam\" >nul
copy  /y "%SDK%\python\setup.py"          "%DIST%\python\" >nul
:: NOTE: the DLL is deliberately NOT copied into python\spwcam\ - it must be
::       loaded from bin\ where all its dependencies live (see _ctypes_wrap.py).
copy /y "%SDK%\examples\c\example_basic.c"        "%DIST%\examples\c\" >nul
copy /y "%SDK%\examples\cpp\example_raii.cpp"     "%DIST%\examples\cpp\" >nul
copy /y "%SDK%\examples\python\example_basic.py"  "%DIST%\examples\python\" >nul
copy /y "%SDK%\examples\CMakeLists.txt"           "%DIST%\examples\" >nul
for %%f in (build.md api_reference.md usage.md troubleshooting.md) do (
    if exist "%SDK%\docs\%%f" copy /y "%SDK%\docs\%%f" "%DIST%\docs\" >nul
)
if exist "%SDK%\README.md" copy /y "%SDK%\README.md" "%DIST%\" >nul

echo [8] QUICKSTART.md...
(
echo # SPWater Camera SDK v%VERSION%
echo Author   : Yuanshi Technology
echo Copyright: Zhoushan Yuanshi Technology Co., Ltd.
echo.
echo == Quick Start ==
echo.
echo The bin\ directory is self-contained: no Qt, GStreamer, FFmpeg or VC++
echo Redistributable installation is required on the target machine.
echo Copy the WHOLE bin\ directory ^(including the gst_plugins\ and
echo imageformats\ sub-directories^) next to your executable.
echo.
echo -- Try it right after unzipping ^(prebuilt examples^) --
echo   bin\example_c_basic.exe   rtsp://^<camera_ip^>:8554/^<path^>
echo   bin\example_cpp_raii.exe  rtsp://^<camera_ip^>:8554/^<path^>
echo   ^(without a URL the examples wait for a camera to be discovered on the LAN^)
echo.
echo -- C/C++ --
echo   1. Include path  : include\
echo   2. Import lib    : lib\spwcam_sdk.lib
echo   3. Runtime       : contents of bin\ next to your .exe
echo.
echo   cl /utf-8 your_app.cpp /I^<SDK^>\include /link ^<SDK^>\lib\spwcam_sdk.lib
echo.
echo -- Python ^(3.8+^) --
echo   cd python
echo   pip install -e .
echo   python ..\examples\python\example_basic.py [rtsp://...]
echo   ^(the package loads bin\spwcam_sdk.dll automatically from the SDK tree;
echo    elsewhere set SPWCAM_SDK_BIN=^<path to bin^>^)
echo.
echo -- Runtime layout --
echo   bin\spwcam_sdk.dll       SDK main DLL
echo   bin\Qt6*.dll             Qt runtime
echo   bin\av*.dll / sw*.dll    FFmpeg runtime
echo   bin\gst*.dll, glib etc.  GStreamer core
echo   bin\gst_plugins\         GStreamer plugins ^(auto-loaded^)
echo   bin\imageformats\        Qt image plugins ^(JPG snapshots^)
echo   bin\vcruntime140.dll     MSVC CRT
echo.
echo See docs\usage.md for the full API guide.
) > "%DIST%\QUICKSTART.md"

echo [9] Dependency closure check (dumpbin)...
set DUMPBIN=
for /f "usebackq delims=" %%p in (`where dumpbin.exe 2^>nul`) do if not defined DUMPBIN set DUMPBIN=%%p
if not defined DUMPBIN (
    for /d %%e in ("C:\Program Files\Microsoft Visual Studio\2022\*") do (
        for /d %%v in ("%%~e\VC\Tools\MSVC\*") do (
            if exist "%%~v\bin\Hostx64\x64\dumpbin.exe" set DUMPBIN=%%~v\bin\Hostx64\x64\dumpbin.exe
        )
    )
)
if not defined DUMPBIN (
    echo     ERROR: dumpbin.exe not found - cannot verify DLL closure. Run from a VS developer prompt.
    set FAIL=1
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_deps.ps1" -Bin "%DIST%\bin" -Dumpbin "%DUMPBIN%"
    if errorlevel 1 (echo     ERROR: dependency closure check FAILED & set FAIL=1) else (echo     OK: all non-system dependencies present)
)

echo.
if "%FAIL%"=="1" (
    echo -------------------------------------------------------
    echo  PACKAGE INCOMPLETE - see ERROR lines above.
    echo -------------------------------------------------------
    exit /b 1
)

echo [10] zip...
if exist "%DIST%.zip" del /q "%DIST%.zip"
powershell -NoProfile -Command "Compress-Archive -Path '%DIST%' -DestinationPath '%DIST%.zip' -Force" >nul 2>&1
if exist "%DIST%.zip" (echo     %DIST%.zip) else (echo     WARN: zip not created)

echo -------------------------------------------------------
echo  Done: %DIST%
echo -------------------------------------------------------
endlocal
exit /b 0
