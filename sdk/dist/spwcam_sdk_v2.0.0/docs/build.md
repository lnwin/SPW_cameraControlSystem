# SPWater Camera SDK — Build Guide

## Prerequisites

| Tool | Minimum version | Notes |
|------|----------------|-------|
| Windows | 10 / 11 x64 | |
| Visual Studio | 2019 or 2022 | MSVC v142/v143, x64 toolset |
| CMake | 3.22 | Add to PATH |
| Qt | 6.5.x (MSVC 2019 64-bit) | Set `QT_ROOT` |
| GStreamer | 1.22.x MSVC x86_64 | Set `GST_ROOT` |
| FFmpeg | 8.0 shared build | Set `FFMPEG_ROOT` |
| Python | 3.8+ (optional) | Only for Python wrapper |

---

## 1. Clone / copy the project

The SDK lives in the `sdk/` subdirectory of the main project:

```
D:\SPwater_CODE\SPW_cameraControlSystem\
├── sdk\           ← SDK source
│   ├── CMakeLists.txt
│   ├── include\
│   ├── src\
│   ├── python\
│   ├── examples\
│   ├── tests\
│   ├── docs\
│   └── scripts\
├── rtspviewerqt.cpp  ← referenced by SDK (not modified)
├── videorecorder.cpp
├── udpserver.cpp
└── ...
```

---

## 2. Configure paths

Edit the top of `sdk/CMakeLists.txt` (or pass on the CMake command line):

```cmake
set(QT_ROOT     "C:/Qt/6.5.3/msvc2019_64")
set(GST_ROOT    "E:/ThirdParty/Gstreamer/1.0/msvc_x86_64")
set(FFMPEG_ROOT "E:/ThirdParty/ffmpeg-8.0-full_build-shared")
```

Or override on command line:

```bat
cmake -DQT_ROOT="C:/Qt/6.5.3/msvc2019_64" ^
      -DGST_ROOT="E:/ThirdParty/Gstreamer/1.0/msvc_x86_64" ^
      -DFFMPEG_ROOT="E:/ThirdParty/ffmpeg-8.0-full_build-shared" ^
      ..
```

---

## 3. Build (manual)

Open **x64 Native Tools Command Prompt for VS 2022**, then:

```bat
cd D:\SPwater_CODE\SPW_cameraControlSystem\sdk
mkdir build && cd build

cmake .. -G "Visual Studio 17 2022" -A x64 ^
         -DCMAKE_BUILD_TYPE=Release

cmake --build . --config Release --parallel
```

Output files in `build\bin\`:
- `spwcam_sdk.dll` — the DLL
- `spwcam_sdk.lib` — import library
- `test_abi.exe`, `test_init.exe`
- `example_c_basic.exe`, `example_cpp_raii.exe`

---

## 4. Build (one-click script)

```bat
scripts\build_sdk.bat
```

See `scripts\build_sdk.bat` for path variables you may need to adjust.

---

## 5. Run tests

```bat
cd build
ctest -C Release -V
```

Expected output:
```
All tests PASSED
```

The ABI test requires no camera. The init test creates and destroys an SDK context.

---

## 6. Package for delivery

```bat
scripts\package_sdk.bat
```

Output in `sdk\dist\`:
```
dist\spwcam_sdk_v2.0.0\
├── bin\
│   ├── spwcam_sdk.dll
│   └── gstreamer\      (GStreamer runtime DLLs)
├── lib\
│   └── spwcam_sdk.lib
├── include\
│   └── spwcam\
│       ├── spwcam.h
│       └── spwcam.hpp
├── python\
│   └── spwcam\         (Python package)
├── examples\
└── docs\
```

---

## 7. Python setup

```bat
cd sdk\dist\spwcam_sdk_v2.0.0\python
pip install -e .
python ..\examples\python\example_basic.py rtsp://<ip>:8554/<path>
```

The package loads `..\..\bin\spwcam_sdk.dll` automatically when it stays inside
the SDK tree.  For any other layout set `SPWCAM_SDK_BIN=<path to the SDK bin
directory>` before importing.  Never copy `spwcam_sdk.dll` alone into
`python\spwcam\` -- its dependencies must be next to it.  Python 3.8+ required.

## 8. Runtime dependencies

`scripts\package_sdk.bat` copies every required DLL into `dist\...\bin\` and
verifies the dependency closure with `dumpbin` (`scripts\check_deps.ps1`);
packaging fails if anything is missing.  Deploy the whole `bin\` directory
(including `gst_plugins\` and `imageformats\`) next to your executable -- no
Qt / GStreamer / FFmpeg / VC++ redistributable installation is needed.

## 9. MSVC Runtime

Build and all dependencies **must** use the same MSVC runtime:

- Release build → `/MD` (MSVCRT.dll)
- Debug build → `/MDd`

Do **not** mix `/MT` (static) with `/MD` (dynamic) DLLs.

---

## 10. Common build errors

| Error | Cause | Fix |
|-------|-------|-----|
| `Qt6CoreConfig.cmake not found` | Wrong `QT_ROOT` | Set correct path |
| `gstreamer-1.0.lib: no such file` | Wrong `GST_ROOT` | Check GStreamer install |
| `LNK2019: unresolved external _av*` | Wrong `FFMPEG_ROOT` | Verify lib names match FFmpeg version |
| MOC errors on Qt classes | `AUTOMOC OFF` | Ensure `set(CMAKE_AUTOMOC ON)` is in CMakeLists |
