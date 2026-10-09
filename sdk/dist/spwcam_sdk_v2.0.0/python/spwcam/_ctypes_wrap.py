"""
spwcam/_ctypes_wrap.py  --  low-level ctypes binding to spwcam_sdk.dll

All public symbols are prefixed spwcam_.
Import this module only via spwcam/__init__.py (or spwcam/camera.py).
"""

import ctypes
import ctypes.util
import os
import sys
import platform
from pathlib import Path

# ──────────────────────────────────────────────────────────── DLL loading ──
#
# spwcam_sdk.dll must be loaded from the SDK `bin` directory because all of its
# runtime dependencies (Qt, GStreamer, FFmpeg, gst_plugins\) live next to it.
# Python >= 3.8 no longer consults PATH when resolving DLL dependencies, so the
# directory is registered explicitly with os.add_dll_directory().
#
# Search order:
#   1. environment variable SPWCAM_SDK_BIN  (directory containing spwcam_sdk.dll)
#   2. <sdk>/bin      when this package is at <sdk>/python/spwcam   (dist layout)
#   3. ./bin, ../bin  relative to the current working directory
#   4. PATH           (legacy fallback)

def _find_dll() -> Path:
    here = Path(__file__).resolve().parent
    candidates = []

    env = os.environ.get("SPWCAM_SDK_BIN")
    if env:
        candidates.append(Path(env) / "spwcam_sdk.dll")

    candidates += [
        here.parent.parent / "bin" / "spwcam_sdk.dll",   # <sdk>/python/spwcam -> <sdk>/bin
        here.parent / "bin" / "spwcam_sdk.dll",
        Path.cwd() / "bin" / "spwcam_sdk.dll",
        Path.cwd().parent / "bin" / "spwcam_sdk.dll",
        here / "spwcam_sdk.dll",                         # only valid if ALL deps are here too
    ]
    for p in candidates:
        if p.is_file():
            return p

    name = ctypes.util.find_library("spwcam_sdk")
    if name:
        return Path(name).resolve()

    raise FileNotFoundError(
        "spwcam_sdk.dll not found. Set SPWCAM_SDK_BIN to the SDK 'bin' directory "
        "(the one that also contains Qt6Core.dll and gst_plugins\\)."
    )


def _load(dll_path: Path):
    bin_dir = str(dll_path.parent)
    # Legacy interpreters / other tools still use PATH
    os.environ["PATH"] = bin_dir + os.pathsep + os.environ.get("PATH", "")
    if hasattr(os, "add_dll_directory"):           # Python >= 3.8 on Windows
        os.add_dll_directory(bin_dir)
        return ctypes.CDLL(str(dll_path))
    return ctypes.CDLL(str(dll_path))


_dll_path = _find_dll()
_lib = _load(_dll_path)

# ──────────────────────────────────────────────────────────── constants ───

SPWCAM_OK                    =   0
SPWCAM_ERR_INVALID_PARAM     =  -1
SPWCAM_ERR_NOT_INITIALIZED   =  -2
SPWCAM_ERR_ALREADY_OPEN      =  -3
SPWCAM_ERR_NOT_OPEN          =  -4
SPWCAM_ERR_DEVICE_NOT_FOUND  =  -5
SPWCAM_ERR_TIMEOUT           =  -6
SPWCAM_ERR_IO                =  -7
SPWCAM_ERR_ENCODER           =  -8
SPWCAM_ERR_NO_FRAME          =  -9
SPWCAM_ERR_OUT_OF_MEMORY     = -10
SPWCAM_ERR_INTERNAL          = -99

SPWCAM_PIXEL_BGRA32 = 0
SPWCAM_PIXEL_RGB24  = 1

SPWCAM_STREAM_STOPPED    = 0
SPWCAM_STREAM_CONNECTING = 1
SPWCAM_STREAM_RUNNING    = 2
SPWCAM_STREAM_ERROR      = 3

SPWCAM_TRIGGER_SOFTWARE = 0
SPWCAM_TRIGGER_HARDWARE = 1

SPWCAM_LOG_DEBUG = 0
SPWCAM_LOG_INFO  = 1
SPWCAM_LOG_WARN  = 2
SPWCAM_LOG_ERROR = 3

SPWCAM_EVENT_DEVICE_DISCOVERED  = 1
SPWCAM_EVENT_DEVICE_LOST        = 2
SPWCAM_EVENT_STREAM_CONNECTED   = 3
SPWCAM_EVENT_STREAM_LOST        = 4
SPWCAM_EVENT_RECORD_STARTED     = 5
SPWCAM_EVENT_RECORD_STOPPED     = 6
SPWCAM_EVENT_RECORD_FAILED      = 7
SPWCAM_EVENT_RECORD_SEGMENT     = 8
SPWCAM_EVENT_SNAPSHOT_SAVED     = 9
SPWCAM_EVENT_TRIGGER_STATUS     = 10
SPWCAM_EVENT_IP_CHANGED         = 11

SPWCAM_CONTAINER_MP4 = 0
SPWCAM_CONTAINER_AVI = 1

SPWCAM_IMAGE_PNG = 0
SPWCAM_IMAGE_JPG = 1
SPWCAM_IMAGE_BMP = 2

# ──────────────────────────────────────────────────────────── structs ─────

class SpwcamInitParams(ctypes.Structure):
    _fields_ = [
        ("struct_size",    ctypes.c_uint32),
        ("discover_port",  ctypes.c_uint16),
        ("heartbeat_port", ctypes.c_uint16),
        ("cmd_port",       ctypes.c_uint16),
        ("log_level",      ctypes.c_int),
    ]

    def __init__(self, discover_port=0, heartbeat_port=0, cmd_port=0,
                 log_level=SPWCAM_LOG_INFO):
        super().__init__()
        self.struct_size    = ctypes.sizeof(self)
        self.discover_port  = discover_port
        self.heartbeat_port = heartbeat_port
        self.cmd_port       = cmd_port
        self.log_level      = log_level


class SpwcamDeviceInfo(ctypes.Structure):
    _fields_ = [
        ("struct_size",  ctypes.c_uint32),
        ("sn",           ctypes.c_char * 64),
        ("ip",           ctypes.c_char * 48),
        ("rtsp_port",    ctypes.c_uint16),
        ("rtsp_path",    ctypes.c_char * 128),
        ("last_seen_ms", ctypes.c_int64),
    ]

    def __init__(self):
        super().__init__()
        self.struct_size = ctypes.sizeof(self)

    @property
    def sn_str(self) -> str:
        return self.sn.decode("utf-8", errors="replace")

    @property
    def ip_str(self) -> str:
        return self.ip.decode("utf-8", errors="replace")

    @property
    def rtsp_path_str(self) -> str:
        return self.rtsp_path.decode("utf-8", errors="replace")

    @property
    def rtsp_url(self) -> str:
        return f"rtsp://{self.ip_str}:{self.rtsp_port}{self.rtsp_path_str}"


class SpwcamFrame(ctypes.Structure):
    _fields_ = [
        ("struct_size",  ctypes.c_uint32),
        ("data",         ctypes.POINTER(ctypes.c_uint8)),
        ("width",        ctypes.c_uint32),
        ("height",       ctypes.c_uint32),
        ("stride",       ctypes.c_uint32),
        ("pixel_format", ctypes.c_int),
        ("timestamp_ms", ctypes.c_int64),
        ("sequence",     ctypes.c_uint64),
    ]


class SpwcamRecordStatus(ctypes.Structure):
    _fields_ = [
        ("struct_size",         ctypes.c_uint32),
        ("recording",           ctypes.c_int),
        ("current_file",        ctypes.c_char * 512),
        ("segment_index",       ctypes.c_int),
        ("segment_elapsed_ms",  ctypes.c_int64),
        ("total_elapsed_ms",    ctypes.c_int64),
    ]

    def __init__(self):
        super().__init__()
        self.struct_size = ctypes.sizeof(self)


class SpwcamRecordOptions(ctypes.Structure):
    _fields_ = [
        ("struct_size",     ctypes.c_uint32),
        ("video_dir",       ctypes.c_char * 512),
        ("snapshot_dir",    ctypes.c_char * 512),
        ("fps",             ctypes.c_int),
        ("bitrate_kbps",    ctypes.c_int),
        ("segment_minutes", ctypes.c_int),
        ("container",       ctypes.c_int),
        ("snapshot_fmt",    ctypes.c_int),
    ]

    def __init__(self, video_dir="", snapshot_dir="", fps=25, bitrate_kbps=8000,
                 segment_minutes=30, container=SPWCAM_CONTAINER_MP4,
                 snapshot_fmt=SPWCAM_IMAGE_PNG):
        super().__init__()
        self.struct_size     = ctypes.sizeof(self)
        self.video_dir       = video_dir.encode("utf-8")
        self.snapshot_dir    = snapshot_dir.encode("utf-8")
        self.fps             = fps
        self.bitrate_kbps    = bitrate_kbps
        self.segment_minutes = segment_minutes
        self.container       = container
        self.snapshot_fmt    = snapshot_fmt


# ──────────────────────────────────────────────────────────── callbacks ───

LogCallbackType   = ctypes.CFUNCTYPE(None, ctypes.c_int,
                                     ctypes.c_char_p, ctypes.c_void_p)
FrameCallbackType = ctypes.CFUNCTYPE(None,
                                     ctypes.POINTER(SpwcamFrame),
                                     ctypes.c_void_p)
EventCallbackType = ctypes.CFUNCTYPE(None, ctypes.c_int,
                                     ctypes.c_char_p, ctypes.c_void_p)

# ──────────────────────────────────────────────── function prototypes ─────

def _fn(name, restype, *argtypes):
    f = getattr(_lib, name)
    f.restype  = restype
    f.argtypes = list(argtypes)
    return f

# version / error
spwcam_version          = _fn("spwcam_version",          ctypes.c_char_p)
spwcam_get_error_string = _fn("spwcam_get_error_string", ctypes.c_char_p,
                               ctypes.c_int)

# lifecycle
spwcam_init             = _fn("spwcam_init",     ctypes.c_void_p,
                               ctypes.POINTER(SpwcamInitParams))
spwcam_deinit           = _fn("spwcam_deinit",   None,  ctypes.c_void_p)
spwcam_get_last_error   = _fn("spwcam_get_last_error", ctypes.c_int,
                               ctypes.c_void_p)

# logging / events
spwcam_set_log_callback   = _fn("spwcam_set_log_callback", None,
                                 ctypes.c_void_p, LogCallbackType, ctypes.c_void_p)
spwcam_set_log_level      = _fn("spwcam_set_log_level", None,
                                 ctypes.c_void_p, ctypes.c_int)
spwcam_set_event_callback = _fn("spwcam_set_event_callback", None,
                                 ctypes.c_void_p, EventCallbackType, ctypes.c_void_p)

# discovery
spwcam_discovery_start     = _fn("spwcam_discovery_start", ctypes.c_int,
                                   ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16)
spwcam_discovery_stop      = _fn("spwcam_discovery_stop", None, ctypes.c_void_p)
spwcam_get_device_count    = _fn("spwcam_get_device_count", ctypes.c_int,
                                   ctypes.c_void_p)
spwcam_get_device_info     = _fn("spwcam_get_device_info", ctypes.c_int,
                                   ctypes.c_void_p, ctypes.c_int,
                                   ctypes.POINTER(SpwcamDeviceInfo))
spwcam_get_device_info_by_sn = _fn("spwcam_get_device_info_by_sn", ctypes.c_int,
                                    ctypes.c_void_p, ctypes.c_char_p,
                                    ctypes.POINTER(SpwcamDeviceInfo))

# device control
spwcam_set_led           = _fn("spwcam_set_led", ctypes.c_int,
                                ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int)
spwcam_set_trigger_mode  = _fn("spwcam_set_trigger_mode", ctypes.c_int,
                                ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int)
spwcam_set_camera_params = _fn("spwcam_set_camera_params", ctypes.c_int,
                                ctypes.c_void_p, ctypes.c_char_p,
                                ctypes.c_int, ctypes.c_double)
spwcam_set_ip            = _fn("spwcam_set_ip", ctypes.c_int,
                                ctypes.c_void_p, ctypes.c_char_p,
                                ctypes.c_char_p, ctypes.c_int)

# stream
spwcam_stream_open       = _fn("spwcam_stream_open", ctypes.c_int,
                                ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int)
spwcam_stream_close      = _fn("spwcam_stream_close", ctypes.c_int, ctypes.c_void_p)
spwcam_stream_get_status = _fn("spwcam_stream_get_status", ctypes.c_int,
                                ctypes.c_void_p)

# frames
spwcam_set_frame_callback = _fn("spwcam_set_frame_callback", None,
                                  ctypes.c_void_p, FrameCallbackType, ctypes.c_void_p)
spwcam_frame_grab         = _fn("spwcam_frame_grab", ctypes.c_int,
                                  ctypes.c_void_p,
                                  ctypes.POINTER(ctypes.POINTER(SpwcamFrame)))
spwcam_frame_free         = _fn("spwcam_frame_free", None,
                                  ctypes.POINTER(SpwcamFrame))

# recording
spwcam_record_set_options = _fn("spwcam_record_set_options", ctypes.c_int,
                                  ctypes.c_void_p,
                                  ctypes.POINTER(SpwcamRecordOptions))
spwcam_record_start       = _fn("spwcam_record_start", ctypes.c_int, ctypes.c_void_p)
spwcam_record_stop        = _fn("spwcam_record_stop",  ctypes.c_int, ctypes.c_void_p)
spwcam_record_get_status  = _fn("spwcam_record_get_status", ctypes.c_int,
                                  ctypes.c_void_p,
                                  ctypes.POINTER(SpwcamRecordStatus))
spwcam_snapshot           = _fn("spwcam_snapshot", ctypes.c_int, ctypes.c_void_p)
