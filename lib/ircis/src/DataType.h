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

  // integer exponentiation -- verbatim
  inline int power(int a, int n) {
    int res = 1;
    while (n) {
      if (n & 1)
        res *= a;
      a *= a;
      n >>= 1;
    }
    return res;
  }

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
    DataType operator / (const DataType& num) const { return DataType(value / num.value, true); }
    DataType operator % (const DataType& num) const { return DataType(value % num.value, true); }
    DataType operator ^ (const DataType& num) const { return DataType(power(value, num.value), true); }
    DataType operator & (const DataType& num) const { return DataType(value & num.value, true); }
    DataType operator | (const DataType& num) const { return DataType(value | num.value, true); }
    DataType V (const DataType& num) const { return DataType(value ^ num.value, true); }
    DataType operator < (const DataType& num) const { return DataType(value << num.value, true); }
    DataType operator > (const DataType& num) const { return DataType(value >> num.value, true); }
  };

  typedef DataType Data;

  inline std::string to_str(const Data& d) { return d.to_string(); }
}
