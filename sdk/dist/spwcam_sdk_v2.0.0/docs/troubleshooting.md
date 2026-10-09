# SPWater Camera SDK — Troubleshooting

## Build Issues

### `Qt6CoreConfig.cmake not found`
**Cause:** CMake cannot locate the Qt6 installation.  
**Fix:** Set `QT_ROOT` to the exact Qt MSVC x64 kit:
```bat
cmake .. -DQT_ROOT="C:/Qt/6.5.3/msvc2019_64"
```
Verify the path contains `lib/cmake/Qt6/Qt6Config.cmake`.

---

### `gstreamer-1.0.lib: No such file`
**Cause:** `GST_ROOT` points to the wrong directory.  
**Fix:** The expected layout is:
```
%GST_ROOT%\lib\gstreamer-1.0.lib
%GST_ROOT%\include\gstreamer-1.0\gst\gst.h
%GST_ROOT%\include\glib-2.0\glib.h
%GST_ROOT%\lib\glib-2.0\include\glibconfig.h
```
If the GStreamer installer places files elsewhere, update `GST_INCLUDE_DIRS` /
`GST_LIBRARIES` in `CMakeLists.txt`.

---

### `LNK2019: unresolved external __imp_avcodec_*`
**Cause:** FFmpeg library names do not match.  
**Fix:** Verify actual `.lib` names in `%FFMPEG_ROOT%\lib\`:
```bat
dir E:\ThirdParty\ffmpeg-8.0-full_build-shared\lib\*.lib
```
Update `FFMPEG_LIBRARIES` in `CMakeLists.txt` to match the installed filenames
(different FFmpeg build variants may use `avcodec.lib` vs `avcodec-62.lib`).

---

### MOC / `Q_OBJECT` compilation errors
**Cause:** `CMAKE_AUTOMOC` is not enabled.  
**Fix:** Confirm the CMakeLists.txt contains:
```cmake
set(CMAKE_AUTOMOC ON)
```
Then do a **clean** rebuild: delete the `build/` directory and reconfigure.

---

### `error C2027: use of undefined type 'UdpDeviceManager'`
**Cause:** The upstream source root is not on the include path.  
**Fix:** Verify the `UPSTREAM_SRC` variable in `CMakeLists.txt`:
```cmake
set(UPSTREAM_SRC "${CMAKE_CURRENT_SOURCE_DIR}/..")
```
This path must contain `udpserver.h`, `rtspviewerqt.h`, `videorecorder.h`, `myStruct.h`.

---

## Runtime Issues

### DLL fails to load (`0xC0000135` / "The specified module could not be found")
**Cause:** A dependency DLL is missing.  The shipped `bin\` directory is
self-contained (it passed a `dumpbin` dependency-closure check at packaging
time), so this normally means only *part* of `bin\` was copied.
**Fix:** Copy the **whole** `bin\` directory, including `gst_plugins\` and
`imageformats\`, next to your executable.  To diagnose, run
`scripts\check_deps.ps1 -Bin <your bin> -Dumpbin <path to dumpbin.exe>` or
`dumpbin /dependents <dll>` from a Visual Studio developer prompt.

---

### GStreamer prints `Failed to load plugin ... gst_plugins\xxx.dll`
**Cause:** A GStreamer *library* DLL that the plugin imports is missing from
`bin\` (plugins live in `gst_plugins\` but their dependencies must be in `bin\`).
With missing plugins the stream stays in CONNECTING forever.
**Fix:** Same as above -- restore the complete `bin\` directory.

---

### `spwcam_init()` returns NULL
**Possible causes:**
1. Another `QCoreApplication` exists but is on a different thread from the Qt
   event loop — SDK cannot share it safely.  
   **Fix:** Call `spwcam_init()` before any Qt GUI code runs, or use the SDK in a
   separate process.
2. An internal exception during Qt thread start.  
   **Fix:** Check the log output; set `log_level = SPWCAM_LOG_DEBUG` before init.

---

### Stream stays in `SPWCAM_STREAM_CONNECTING`
**Possible causes:**
- Wrong RTSP URL -- double-check IP, port, path.
- Device not reachable from PC (firewall, VLAN, wrong subnet).
- GStreamer plugins failed to load (see the section above).

**Debug steps:**
1. Enable debug logs: `spwcam_set_log_level(ctx, SPWCAM_LOG_DEBUG)`.
2. Look for `[GST]` messages in the log. `[GST][ERR] ... Failed to connect`
   means the RTSP server is unreachable; `Failed to load plugin` means the
   `bin\` directory is incomplete.
3. Check Windows Firewall -- the SDK receives RTP over UDP on dynamic ports;
   allow the application through the firewall (inbound UDP).

### Stream switches to `SPWCAM_STREAM_ERROR`
No frame was received for 5 s after the stream had been running
(`SPWCAM_EVENT_STREAM_LOST`).  The SDK reconnects automatically; status
returns to RUNNING and `SPWCAM_EVENT_STREAM_CONNECTED` fires when frames resume.

---

### `spwcam_frame_grab` always returns `SPWCAM_ERR_NO_FRAME`
**Cause:** The frame pump has not yet received a frame since the last call.  
This is normal for the first ~1 s after stream open, or at 25 fps when polling faster.

**Fix:**  
- Wait at least 1 s after `spwcam_stream_open()` before grabbing.
- Use push-mode (`spwcam_set_frame_callback`) to receive frames as they arrive.
- Check stream status: `spwcam_stream_get_status()` must return `SPWCAM_STREAM_RUNNING`.

---

### `spwcam_record_start()` returned OK but nothing is recorded
Recording starts asynchronously: the encoder opens on the next frame, then
`SPWCAM_EVENT_RECORD_STARTED` fires and `spwcam_record_get_status()` reports
`recording = 1`.  If `SPWCAM_EVENT_RECORD_FAILED` fires instead, the log
contains the reason (usually the output directory cannot be created or the
disk is full).

### Recording file is unplayable
The MP4 index is written when the file is closed.  Always call
`spwcam_record_stop()` (and wait for `SPWCAM_EVENT_RECORD_STOPPED`) or
`spwcam_deinit()` -- the latter finalises the file synchronously.  Killing the
process while recording leaves the last file without an index.

---

### `spwcam_discovery_start()` returns `SPWCAM_ERR_IO`
UDP ports 7776 / 8888 could not be bound.  Another process -- typically the
desktop camera application -- already owns them.  Close it, or pass different
ports (the device must be configured to match).

---

### Python: `FileNotFoundError: spwcam_sdk.dll not found`
The package looks for `<sdk>\bin\spwcam_sdk.dll` relative to `python\spwcam\`
(the shipped layout), then `.\bin`, `..\bin` relative to the working directory.
**Fix:** keep the SDK directory tree intact, or set the environment variable
`SPWCAM_SDK_BIN` to the directory that contains `spwcam_sdk.dll` **and all of
its dependencies** (the SDK `bin\` directory).  Do not copy the DLL alone into
`python\spwcam\`.

---

### Python: `OSError: [WinError 126] The specified module could not be found`
**Cause:** `spwcam_sdk.dll` was found but one of **its** dependencies is
missing next to it.  Python 3.8+ does not use `PATH` for DLL dependencies.
**Fix:** point `SPWCAM_SDK_BIN` at the complete SDK `bin\` directory (the
package registers it with `os.add_dll_directory()` automatically).

---

### Python numpy array is wrong colour (red/blue swapped)
**Cause:** `frame_grab_numpy()` returns BGRA (not RGBA).  
**Fix:**
```python
arr = cam.frame_grab_numpy()          # BGRA
rgb = arr[:, :, [2, 1, 0]]           # → RGB (drop alpha)
# or for OpenCV (which also uses BGR):
bgr = arr[:, :, :3]                   # → BGR directly
```

---

## Logging Tips

Turn on maximum verbosity to diagnose startup / stream issues:

```c
spwcam_set_log_level(ctx, SPWCAM_LOG_DEBUG);
spwcam_set_log_callback(ctx, my_log_fn, NULL);
```

Key log prefixes:
- `[SDK]` — SDK adapter layer
- `[GST]` — GStreamer pipeline messages
- `[UDP]` — UDP device manager
- `[VideoRecorder]` — FFmpeg encoder
- `[PERF]` — frame timing statistics (debug builds)
- `[IPCFG-PC]` — IP change diagnostics
