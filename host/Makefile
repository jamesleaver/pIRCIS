# Copyright (c) 2026 James Leaver.
# SPDX-License-Identifier: MIT
# pIRCIS -- https://github.com/jamesleaver/pIRCIS
#
# The IRCIS interpreter under lib/ircis/ is Arjun Nair's work, MIT licensed,
# and is not covered by this notice.

# Host build: same core as the firmware, compiled for macOS/Linux.
#   make        -> build sk_emu and the golden test
#   make test   -> run the golden test
CXX      ?= c++
CXXFLAGS ?= -std=gnu++17 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES  = -I../lib/ircis/src -I../lib/program/src -I../lib/pack/src

# Some macOS Command Line Tools installs ship libc++ only inside the SDK, where
# clang does not look by default (every #include <string> then fails). Detect
# that case and add the SDK's C++ headers explicitly.
SDK_CXX := $(shell p=$$(xcrun --show-sdk-path 2>/dev/null)/usr/include/c++/v1; \
                   [ -e "$$p/string" ] && ! echo '\#include <string>' | \
                   $(CXX) -x c++ -std=gnu++17 -fsyntax-only - >/dev/null 2>&1 && \
                   echo -isystem $$p)
INCLUDES += $(SDK_CXX)
CORE      = ../lib/ircis/src/CharMaps.cpp ../lib/ircis/src/DataType.cpp \
            ../lib/ircis/src/DirVec.cpp ../lib/ircis/src/Grid.cpp \
            ../lib/ircis/src/Logger.cpp ../lib/ircis/src/Runner.cpp \
            ../lib/ircis/src/Machine.cpp ../lib/program/src/Program.cpp \
            ../lib/pack/src/Pack.cpp ../lib/pack/src/Crypto.cpp

# Headers, so a change to Examples.h or Pack.h rebuilds what depends on it.
# Without this a regenerated header left a stale object behind and the tests
# reported on code that was no longer there.
HDRS = $(wildcard ../lib/ircis/src/*.h ../lib/program/src/*.h ../lib/pack/src/*.h)

all: sk_emu test_golden test_crypto test_pack

sk_emu: sk_emu.cpp $(CORE) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(filter %.cpp,$^)

test_golden: test_golden.cpp $(CORE) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(filter %.cpp,$^)

test_crypto: test_crypto.cpp ../lib/pack/src/Crypto.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(filter %.cpp,$^)

test_pack: test_pack.cpp $(CORE) $(HDRS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $(filter %.cpp,$^)

test: test_golden test_crypto test_pack
	./test_crypto
	./test_pack
	./test_golden

clean:
	rm -f sk_emu test_golden test_crypto test_pack

.PHONY: all test clean
