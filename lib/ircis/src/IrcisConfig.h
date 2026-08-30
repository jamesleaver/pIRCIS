// Ported from IRCIS -- "I Run Chars I See" -- by Arjun Nair (batman-nair):
//   https://github.com/batman-nair/IRCIS
//
// Copyright (c) 2019 Arjun Nair
// Licensed under the MIT License. See lib/ircis/LICENSE for the full text.
//
// Modified for pIRCIS by James Leaver: bounded memory, no iostream
// or filesystem, a seeded RNG and a ring-buffer trail, so the interpreter
// runs unchanged on an ESP32. Behaviour is deliberately byte-identical to
// the reference build.

#pragma once
// Compile-time knobs for the portable IRCIS core.
//
// The core is a faithful port of batman-nair/IRCIS (src/ in IRCIS-master).
// Everything that touched the filesystem, iostreams or unbounded memory has
// been replaced; the *semantics* of Runner::step() are unchanged, because a
// program's behaviour can depend on exact execution timing.

// Emit the interpreter's internal DEBUG/ERROR trace. Off by default: on the
// original desktop build this wrote ~8 MB to debug.log per run.
#ifndef IRCIS_DEBUG_LOG
#define IRCIS_DEBUG_LOG 0
#endif

// Record every character each Runner processed (only used by run_debug()).
// A long run processes hundreds of thousands of chars per runner.
#ifndef IRCIS_TRACK_PROCESSED_CHARS
#define IRCIS_TRACK_PROCESSED_CHARS 0
#endif

// Length of the fixed-size position trail kept per Runner for rendering.
// The original kept an unbounded std::vector<DirVec> (~2.8 MB for a full run,
// which is ~10x the ESP32's usable heap).
//
// Keep this equal to run::kTrailView: every entry is copied whenever a runner
// splits, so allocating more than the UI draws is pure cost.
#ifndef IRCIS_TRAIL_LEN
#define IRCIS_TRAIL_LEN 24
#endif
