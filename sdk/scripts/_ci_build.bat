@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set SDK_DIR=D:\SPwater_CODE\SPW_cameraControlSystem\sdk
set BUILD_DIR=%SDK_DIR%\build_ci

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo [STEP 1] CMake configure...
cmake "%SDK_DIR%" ^
  -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DQT_ROOT=C:/Qt/6.4.3/msvc2019_64" ^
  "-DGST_ROOT=E:/ThirdParty/Gstreamer/1.0/msvc_x86_64" ^
  "-DFFMPEG_ROOT=E:/ThirdParty/ffmpeg-8.0-full_build-shared" ^
  -DSPWCAM_BUILD_EXAMPLES=OFF ^
  -DSPWCAM_BUILD_TESTS=ON
if errorlevel 1 (echo [FAILED] configure && exit /b 1)

echo [STEP 2] Build Release...
cmake --build . --config Release --parallel
if errorlevel 1 (echo [FAILED] build && exit /b 1)

echo [STEP 3] ctest...
set PATH=C:\Qt\6.4.3\msvc2019_64\bin;E:\ThirdParty\Gstreamer\1.0\msvc_x86_64\bin;E:\ThirdParty\ffmpeg-8.0-full_build-shared\bin;%PATH%
ctest -C Release --output-on-failure
echo [DONE] exit=%ERRORLEVEL%
