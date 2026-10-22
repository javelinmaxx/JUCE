#!/usr/bin/env python3
"""Run a native editor test on bare Xvfb without a window manager."""
import ctypes
import subprocess
import sys


def main():
    x11 = ctypes.CDLL("libX11.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    x11.XInternAtom.restype = ctypes.c_ulong
    x11.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    display = x11.XOpenDisplay(None)
    if not display:
        raise RuntimeError("No X11 display; run this command through xvfb-run.")
    try:
        # JUCE queries these existing atoms when creating a real native peer.
        # Keep this connection open until the child exits: otherwise Xvfb may
        # reset the server and remove the atoms before JUCE connects.
        for name in ("WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW",
                     "WM_STATE", "WM_CHANGE_STATE", "_NET_WM_PING",
                     "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL",
                     "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_STATE",
                     "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ",
                     "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_FULLSCREEN",
                     "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS", "_NET_WORKAREA",
                     "_NET_WM_ALLOWED_ACTIONS", "_MOTIF_WM_HINTS", "_WIN_HINTS",
                     "KWM_WIN_DECORATION", "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE"):
            x11.XInternAtom(display, name.encode(), False)
        x11.XSync(display, False)
        return subprocess.call(sys.argv[1:])
    finally:
        x11.XCloseDisplay(display)


if __name__ == "__main__":
    sys.exit(main())
