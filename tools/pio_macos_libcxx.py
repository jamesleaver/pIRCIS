# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

"""Work around macOS Command Line Tools installs that ship libc++ only inside
the SDK, where clang does not look. Without this every #include <string> fails.

Detects the broken layout and adds the SDK's C++ headers explicitly; a healthy
install is left untouched. host/Makefile does the same thing for the non-PIO
build.
"""
import os
import subprocess
import sys

Import("env")  # noqa: F821  (injected by SCons)

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
            env.Append(CXXFLAGS=["-isystem", sdk_cxx])
            print("macos: using SDK libc++ headers at %s" % sdk_cxx)
