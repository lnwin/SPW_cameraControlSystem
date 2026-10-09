# SPWater Camera SDK v2.0.0
Author   : Yuanshi Technology
Copyright: Zhoushan Yuanshi Technology Co., Ltd.

== Quick Start ==

The bin\ directory is self-contained: no Qt, GStreamer, FFmpeg or VC++
Redistributable installation is required on the target machine.
Copy the WHOLE bin\ directory (including the gst_plugins\ and
imageformats\ sub-directories) next to your executable.

-- Try it right after unzipping (prebuilt examples) --
  bin\example_c_basic.exe   rtsp://<camera_ip>:8554/<path>
  bin\example_cpp_raii.exe  rtsp://<camera_ip>:8554/<path>
  (without a URL the examples wait for a camera to be discovered on the LAN)

-- C/C++ --
  1. Include path  : include\
  2. Import lib    : lib\spwcam_sdk.lib
  3. Runtime       : contents of bin\ next to your .exe

  cl /utf-8 your_app.cpp /I<SDK>\include /link <SDK>\lib\spwcam_sdk.lib

-- Python (3.8+) --
  cd python
  pip install -e .
  python ..\examples\python\example_basic.py [rtsp://...]
  (the package loads bin\spwcam_sdk.dll automatically from the SDK tree;
   elsewhere set SPWCAM_SDK_BIN=<path to bin>)

-- Runtime layout --
  bin\spwcam_sdk.dll       SDK main DLL
  bin\Qt6*.dll             Qt runtime
  bin\av*.dll / sw*.dll    FFmpeg runtime
  bin\gst*.dll, glib etc.  GStreamer core
  bin\gst_plugins\         GStreamer plugins (auto-loaded)
  bin\imageformats\        Qt image plugins (JPG snapshots)
  bin\vcruntime140.dll     MSVC CRT

See docs\usage.md for the full API guide.
