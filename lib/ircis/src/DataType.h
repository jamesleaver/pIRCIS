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

#include <string>

namespace ircis {

  // integer exponentiation -- verbatim for a non-negative exponent. The
  // original never returned for a negative one: the shift that counts the
  // exponent down stops at -1 and stays there. Integer arithmetic has no
  // fractions, so a negative exponent gives what truncation gives -- 1 for a
  // base of 1, plus or minus 1 for a base of -1, 0 for anything else.
  inline int power(int a, int n) {
    if (n < 0) return a == 1 ? 1 : (a == -1 ? ((n & 1) ? -1 : 1) : 0);
    int res = 1;
    while (n) {
      if (n & 1)
        res *= a;
      a *= a;
      n >>= 1;
    }
    return res;
  }

  // Arithmetic wraps, two's complement, and the builds say so (-fwrapv):
  // programs count on it. These four make the cases the hardware treats
  // differently come out the same everywhere. INT_MIN divided by -1 is
  // INT_MIN on the board's divider and a trap on an x86; a shift count is
  // taken modulo 32 by every target's shifter, which C++ leaves open.
  inline int idiv(int a, int b) { return b == -1 ? (int)(0u - (unsigned)a) : a / b; }
  inline int imod(int a, int b) { return b == -1 ? 0 : a % b; }
  inline int ishl(int a, int n) { return (int)((unsigned)a << (n & 31)); }
  inline int ishr(int a, int n) { return a >> (n & 31); }

  // Structure to store values taken from grid in stack
  struct DataType {
    DataType() : value(0), is_integer(true) { }
    DataType(int value, bool is_integer = false)
      : value(value), is_integer(is_integer) { }

    int value;
    bool is_integer;

    std::string to_string() const;

    DataType operator + (const DataType& num) const { return DataType(value + num.value, true); }
    DataType operator - (const DataType& num) const { return DataType(value - num.value, true); }
    DataType operator * (const DataType& num) const { return DataType(value * num.value, true); }
    DataType operator / (const DataType& num) const { return DataType(idiv(value, num.value), true); }
    DataType operator % (const DataType& num) const { return DataType(imod(value, num.value), true); }
    DataType operator ^ (const DataType& num) const { return DataType(power(value, num.value), true); }
    DataType operator & (const DataType& num) const { return DataType(value & num.value, true); }
    DataType operator | (const DataType& num) const { return DataType(value | num.value, true); }
    DataType V (const DataType& num) const { return DataType(value ^ num.value, true); }
    DataType operator < (const DataType& num) const { return DataType(ishl(value, num.value), true); }
    DataType operator > (const DataType& num) const { return DataType(ishr(value, num.value), true); }
  };

  typedef DataType Data;

  inline std::string to_str(const Data& d) { return d.to_string(); }
}
