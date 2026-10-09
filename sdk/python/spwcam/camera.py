"""
spwcam/camera.py  --  high-level Python API wrapping the ctypes bindings

Usage:
    from spwcam import Camera

    cam = Camera()
    cam.on_log(lambda level, msg: print(msg))   # may be set before or after init()
    cam.init()
    cam.discovery_start()
    dev = cam.wait_for_device(10.0)
    if dev:
        cam.stream_open(dev.rtsp_url)
        cam.wait_for_stream(15.0)
        frame = cam.frame_grab_numpy()   # HxWx4 BGRA numpy array or None
        cam.record_start()
        cam.snapshot()
        cam.record_stop()
    cam.deinit()

Callbacks run on the SDK's internal thread; calling Camera methods from inside
a callback is allowed.
"""

from __future__ import annotations

import ctypes
import os
import time
from typing import Callable, Optional, List

from . import _ctypes_wrap as _c

__all__ = ["Camera", "CameraError", "DeviceInfo", "RecordStatus"]


class CameraError(Exception):
    def __init__(self, code: int):
        msg = _c.spwcam_get_error_string(code)
        if msg:
            msg = msg.decode("utf-8", errors="replace")
        super().__init__(f"[{code}] {msg}")
        self.code = code


def _check(rc: int) -> None:
    if rc != _c.SPWCAM_OK:
        raise CameraError(rc)


class DeviceInfo:
    def __init__(self, raw: _c.SpwcamDeviceInfo):
        self.sn           = raw.sn_str
        self.ip           = raw.ip_str
        self.rtsp_port    = raw.rtsp_port
        self.rtsp_path    = raw.rtsp_path_str
        self.last_seen_ms = raw.last_seen_ms

    @property
    def rtsp_url(self) -> str:
        return f"rtsp://{self.ip}:{self.rtsp_port}{self.rtsp_path}"

    def __repr__(self) -> str:
        return f"DeviceInfo(sn={self.sn!r}, ip={self.ip!r}, rtsp_port={self.rtsp_port})"


class RecordStatus:
    def __init__(self, raw: _c.SpwcamRecordStatus):
        self.recording          = bool(raw.recording)
        self.current_file       = raw.current_file.decode("utf-8", errors="replace")
        self.segment_index      = raw.segment_index
        self.segment_elapsed_ms = raw.segment_elapsed_ms
        self.total_elapsed_ms   = raw.total_elapsed_ms

    def __repr__(self) -> str:
        return (f"RecordStatus(recording={self.recording}, "
                f"file={self.current_file!r}, "
                f"elapsed={self.total_elapsed_ms}ms)")


class Camera:
    """High-level Python wrapper around spwcam_sdk.dll."""

    def __init__(self):
        self._ctx  = None
        # Keep references to ctypes callbacks to prevent GC
        self._log_cb_ref    = None
        self._event_cb_ref  = None
        self._frame_cb_ref  = None
        # Python-level callbacks
        self._py_log_cb    = None
        self._py_event_cb  = None
        self._py_frame_cb  = None

    # ------------------------------------------------------------------ lifecycle

    def init(self, discover_port: int = 0, heartbeat_port: int = 0,
             cmd_port: int = 0, log_level: int = _c.SPWCAM_LOG_INFO) -> None:
        """Initialise the SDK and start the internal Qt event loop."""
        if self._ctx is not None:
            raise CameraError(_c.SPWCAM_ERR_ALREADY_OPEN)

        params = _c.SpwcamInitParams(
            discover_port  = discover_port,
            heartbeat_port = heartbeat_port,
            cmd_port       = cmd_port,
            log_level      = log_level,
        )
        ctx = _c.spwcam_init(ctypes.byref(params))
        if ctx is None:
            raise CameraError(_c.SPWCAM_ERR_INTERNAL)
        self._ctx = ctx

        # register callbacks that were set before init()
        if self._py_log_cb   is not None: self.on_log(self._py_log_cb)
        if self._py_event_cb is not None: self.on_event(self._py_event_cb)
        if self._py_frame_cb is not None: self.on_frame(self._py_frame_cb)

    def deinit(self) -> None:
        """Release all SDK resources."""
        if self._ctx is None:
            return
        _c.spwcam_deinit(self._ctx)
        self._ctx = None
        self._log_cb_ref = None
        self._event_cb_ref = None
        self._frame_cb_ref = None

    def __enter__(self):
        self.init()
        return self

    def __exit__(self, *_):
        self.deinit()

    # ------------------------------------------------------------------ callbacks
    # All three may be called before init(); they are applied once the context exists.

    def on_log(self, callback: Optional[Callable]) -> None:
        """Set a Python log callback: callback(level: int, message: str)."""
        self._py_log_cb = callback
        if self._ctx is None:
            return
        if callback is None:
            _c.spwcam_set_log_callback(self._ctx, _c.LogCallbackType(), None)
            self._log_cb_ref = None
            return

        def _cb(level, msg, _ud):
            try:
                callback(level, msg.decode("utf-8", errors="replace") if msg else "")
            except Exception:
                pass

        self._log_cb_ref = _c.LogCallbackType(_cb)
        _c.spwcam_set_log_callback(self._ctx, self._log_cb_ref, None)

    def on_event(self, callback: Optional[Callable]) -> None:
        """Set a Python event callback: callback(event_type: int, data: str)."""
        self._py_event_cb = callback
        if self._ctx is None:
            return
        if callback is None:
            _c.spwcam_set_event_callback(self._ctx, _c.EventCallbackType(), None)
            self._event_cb_ref = None
            return

        def _cb(ev, data, _ud):
            try:
                callback(ev, data.decode("utf-8", errors="replace") if data else "")
            except Exception:
                pass

        self._event_cb_ref = _c.EventCallbackType(_cb)
        _c.spwcam_set_event_callback(self._ctx, self._event_cb_ref, None)

    def on_frame(self, callback: Optional[Callable]) -> None:
        """Set a per-frame push callback: callback(frame_ptr: ctypes pointer).
        For numpy arrays use frame_grab_numpy() in pull mode instead."""
        self._py_frame_cb = callback
        if self._ctx is None:
            return
        if callback is None:
            _c.spwcam_set_frame_callback(self._ctx, _c.FrameCallbackType(), None)
            self._frame_cb_ref = None
            return

        def _cb(frame_ptr, _ud):
            try:
                callback(frame_ptr)
            except Exception:
                pass

        self._frame_cb_ref = _c.FrameCallbackType(_cb)
        _c.spwcam_set_frame_callback(self._ctx, self._frame_cb_ref, None)

    # ------------------------------------------------------------------ discovery

    def discovery_start(self, discover_port: int = 0,
                        heartbeat_port: int = 0) -> None:
        _check(_c.spwcam_discovery_start(self._ctx, discover_port, heartbeat_port))

    def discovery_stop(self) -> None:
        _c.spwcam_discovery_stop(self._ctx)

    def device_count(self) -> int:
        return _c.spwcam_get_device_count(self._ctx)

    def device_info(self, index: int) -> DeviceInfo:
        raw = _c.SpwcamDeviceInfo()
        _check(_c.spwcam_get_device_info(self._ctx, index, ctypes.byref(raw)))
        return DeviceInfo(raw)

    def device_info_by_sn(self, sn: str) -> DeviceInfo:
        raw = _c.SpwcamDeviceInfo()
        _check(_c.spwcam_get_device_info_by_sn(
            self._ctx, sn.encode("utf-8"), ctypes.byref(raw)))
        return DeviceInfo(raw)

    def all_devices(self) -> List[DeviceInfo]:
        return [self.device_info(i) for i in range(self.device_count())]

    def wait_for_device(self, timeout_s: float = 10.0,
                        poll_interval_s: float = 0.5) -> Optional[DeviceInfo]:
        """Block until at least one device is found or timeout expires."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.device_count() > 0:
                return self.device_info(0)
            time.sleep(poll_interval_s)
        return None

    # ------------------------------------------------------------------ control

    def set_led(self, sn: str, enable: bool) -> None:
        _check(_c.spwcam_set_led(self._ctx, sn.encode("utf-8"), int(enable)))

    def set_trigger_mode(self, sn: str, hardware: bool = False) -> None:
        mode = _c.SPWCAM_TRIGGER_HARDWARE if hardware else _c.SPWCAM_TRIGGER_SOFTWARE
        _check(_c.spwcam_set_trigger_mode(self._ctx, sn.encode("utf-8"), mode))

    def set_camera_params(self, sn: str,
                          exposure_us: int = 20000,
                          gain_db: float = 7.5) -> None:
        _check(_c.spwcam_set_camera_params(
            self._ctx, sn.encode("utf-8"), exposure_us, gain_db))

    def set_ip(self, sn: str, new_ip: str, mask: int = 24) -> None:
        _check(_c.spwcam_set_ip(
            self._ctx, sn.encode("utf-8"), new_ip.encode("utf-8"), mask))

    # ------------------------------------------------------------------ stream

    def stream_open(self, rtsp_url: str, latency_ms: int = 0) -> None:
        _check(_c.spwcam_stream_open(
            self._ctx, rtsp_url.encode("utf-8"), latency_ms))

    def stream_close(self) -> None:
        _check(_c.spwcam_stream_close(self._ctx))

    def stream_status(self) -> int:
        return _c.spwcam_stream_get_status(self._ctx)

    def stream_running(self) -> bool:
        return self.stream_status() == _c.SPWCAM_STREAM_RUNNING

    def wait_for_stream(self, timeout_s: float = 15.0,
                        poll_interval_s: float = 0.2) -> bool:
        """Block until stream status is RUNNING or timeout."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            s = self.stream_status()
            if s == _c.SPWCAM_STREAM_RUNNING:
                return True
            if s == _c.SPWCAM_STREAM_ERROR:
                return False
            time.sleep(poll_interval_s)
        return False

    # ------------------------------------------------------------------ frames

    def frame_grab(self) -> Optional[_c.SpwcamFrame]:
        """Pull the latest frame.  Returns None if no new frame available.
        Caller MUST call spwcam_frame_free() or use frame_grab_numpy()."""
        ptr = ctypes.POINTER(_c.SpwcamFrame)()
        rc  = _c.spwcam_frame_grab(self._ctx, ctypes.byref(ptr))
        if rc == _c.SPWCAM_ERR_NO_FRAME or not ptr:
            return None
        _check(rc)
        return ptr

    def frame_grab_numpy(self):
        """Pull the latest frame as a numpy array (H x W x 4, BGRA, uint8).
        Returns None if no new frame is available.
        Requires numpy."""
        import numpy as np

        ptr = self.frame_grab()
        if ptr is None:
            return None

        f  = ptr.contents
        h, w, st = f.height, f.width, f.stride
        # Create numpy array that shares memory with SDK buffer
        arr = np.ctypeslib.as_array(f.data, shape=(h * st,)).copy()
        _c.spwcam_frame_free(ptr)

        # Reshape to H x W x 4 (stride may be padded, slice to actual width)
        arr = arr.reshape(h, st)[:, : w * 4].reshape(h, w, 4)
        return arr

    # ------------------------------------------------------------------ recording

    def record_set_options(self, video_dir: Optional[str] = None,
                            snapshot_dir: Optional[str] = None,
                            fps: int = 25, bitrate_kbps: int = 8000,
                            segment_minutes: int = 30,
                            container: int = _c.SPWCAM_CONTAINER_MP4,
                            snapshot_fmt: int = _c.SPWCAM_IMAGE_PNG) -> None:
        """Configure output directories.  Defaults: ./spwcam_record, ./spwcam_snapshot.
        fps / bitrate_kbps / segment_minutes are fixed at 25 / 8000 / 30 in SDK v2.x."""
        if video_dir is None:
            video_dir = os.path.join(os.getcwd(), "spwcam_record")
        if snapshot_dir is None:
            snapshot_dir = os.path.join(os.getcwd(), "spwcam_snapshot")
        opts = _c.SpwcamRecordOptions(
            video_dir       = video_dir,
            snapshot_dir    = snapshot_dir,
            fps             = fps,
            bitrate_kbps    = bitrate_kbps,
            segment_minutes = segment_minutes,
            container       = container,
            snapshot_fmt    = snapshot_fmt,
        )
        _check(_c.spwcam_record_set_options(self._ctx, ctypes.byref(opts)))

    def record_start(self) -> None:
        _check(_c.spwcam_record_start(self._ctx))

    def record_stop(self) -> None:
        _check(_c.spwcam_record_stop(self._ctx))

    def record_status(self) -> RecordStatus:
        raw = _c.SpwcamRecordStatus()
        _check(_c.spwcam_record_get_status(self._ctx, ctypes.byref(raw)))
        return RecordStatus(raw)

    def recording(self) -> bool:
        return self.record_status().recording

    def snapshot(self) -> None:
        _check(_c.spwcam_snapshot(self._ctx))

    # ------------------------------------------------------------------ version

    @staticmethod
    def version() -> str:
        v = _c.spwcam_version()
        return v.decode("utf-8") if v else "unknown"
