# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

"""Find SDL2 for the desktop build, on whichever machine is doing the building.

Asks pkg-config or sdl2-config first, since a working install answers for
itself and gets the flags right. Falls back to the usual places each system
puts it. Also works around macOS Command Line Tools installs that ship libc++
only inside the SDK, where clang does not look, without which every
#include <string> fails.
"""
import os
import shutil
import subprocess
import sys

Import("env")  # noqa: F821  (injected by SCons)

WINDOWS = sys.platform.startswith("win")


def ask(cmd):
    """Flags from a helper tool, or None if it is not there or fails."""
    if not shutil.which(cmd[0]):
        return None
    try:
        return subprocess.check_output(cmd, text=True).split()
    except Exception:
        return None


def apply_flags(flags):
    for f in flags:
        if f.startswith("-I"):
            env.Append(CPPPATH=[f[2:]])              # noqa: F821
        elif f.startswith("-L"):
            env.Append(LIBPATH=[f[2:]])              # noqa: F821
        elif f.startswith("-l"):
            env.Append(LIBS=[f[2:]])                 # noqa: F821
        elif f.startswith("-D"):
            env.Append(CPPDEFINES=[f[2:]])           # noqa: F821
        else:
            env.Append(LINKFLAGS=[f])                # noqa: F821


cflags = ask(["pkg-config", "--cflags", "sdl2"]) or ask(["sdl2-config", "--cflags"])
libs   = ask(["pkg-config", "--libs", "sdl2"])   or ask(["sdl2-config", "--libs"])

if libs and WINDOWS:
    # pkg-config on MSYS2 adds -mwindows, which makes a program with no
    # console. The emulator reads its commands from the console, and the
    # screenshot harness talks to it there, so it keeps one.
    libs = [f for f in libs if f != "-mwindows"]
if WINDOWS:
    # The link line is longer than Windows allows, so SCons writes it to a
    # temporary file and afterwards deletes that file with a cmd.exe
    # built-in. By default the file goes in the build directory, whose path
    # arrives with forward slashes when it was given as ~/..., and cmd.exe
    # reads C:/Users as a switch. A directory in Windows spelling fixes it.
    import tempfile
    env.Replace(TEMPFILEDIR=os.path.normpath(tempfile.gettempdir()).replace("/", "\\"))  # noqa: F821
if cflags and libs:
    apply_flags(cflags)
    apply_flags(libs)
    print("sdl2: flags from pkg-config/sdl2-config")
else:
    # No helper tool. Look where each system usually puts it.
    if WINDOWS:
        roots = [os.environ.get("SDL2_DIR", ""), r"C:\msys64\mingw64",
                 r"C:\SDL2", r"C:\tools\SDL2"]
        found = False
        for r in roots:
            if r and os.path.exists(os.path.join(r, "include", "SDL2", "SDL.h")):
                env.Append(CPPPATH=[os.path.join(r, "include", "SDL2")])   # noqa: F821
                env.Append(LIBPATH=[os.path.join(r, "lib")])               # noqa: F821
                print("sdl2: found under %s" % r)
                found = True
                break
        if not found:
            print("sdl2: not found. Install MSYS2 and 'pacman -S "
                  "mingw-w64-x86_64-SDL2', or set SDL2_DIR to where SDL2 lives.")
        # SDL takes over main() on Windows and needs these three, in this order.
        env.Append(LIBS=["mingw32", "SDL2main", "SDL2"])                   # noqa: F821
    else:
        for inc in ("/opt/homebrew/include/SDL2", "/usr/local/include/SDL2",
                    "/usr/include/SDL2"):
            if os.path.exists(os.path.join(inc, "SDL.h")):
                env.Append(CPPPATH=[inc])                                  # noqa: F821
        for lib in ("/opt/homebrew/lib", "/usr/local/lib"):
            if os.path.isdir(lib):
                env.Append(LIBPATH=[lib])                                  # noqa: F821
        env.Append(LIBS=["SDL2"])                                          # noqa: F821

# pthread is a POSIX idea; on Windows the threads come with the runtime.
if not WINDOWS:
    env.Append(LIBS=["pthread"])                                           # noqa: F821

if sys.platform == "darwin":
    clang_own = "/Library/Developer/CommandLineTools/usr/include/c++/v1/string"
    if not os.path.exists(clang_own):
        try:
            sdk = subprocess.check_output(
                ["xcrun", "--show-sdk-path"], text=True).strip()
        except Exception:
            sdk = ""
        sdk_cxx = os.path.join(sdk, "usr", "include", "c++", "v1")
        if sdk and os.path.exists(os.path.join(sdk_cxx, "string")):
            env.Append(CXXFLAGS=["-isystem", sdk_cxx])                     # noqa: F821
            print("macos: using SDK libc++ headers at %s" % sdk_cxx)
