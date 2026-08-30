// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Two primitives and a key schedule, kept deliberately small.
//
// SHA-256 is here rather than pulled from mbedtls so that the firmware, the
// desktop emulator and the Python tool that builds the pack all agree byte for
// byte -- the tool uses hashlib, and a standard algorithm is the only way to
// be sure they match without shipping a third implementation.
//
// TEA is the cipher the payload itself uses, which is the reason it is the
// cipher here too. It is 1994 vintage and nobody should protect anything
// valuable with it; that is not what it is doing. It is small, it is exact,
// and it costs an ESP32 nothing.
namespace crypto {

  constexpr std::size_t kSha256Bytes = 32;
  constexpr std::size_t kTeaKeyBytes = 16;   // 128-bit key
  constexpr std::size_t kTeaBlock    = 8;    // 64-bit block

  void sha256(const uint8_t* data, std::size_t len, uint8_t out[kSha256Bytes]);
  void sha256(const std::string& s, uint8_t out[kSha256Bytes]);

  // One TEA block, big-endian, 32 rounds. In place.
  void teaEncryptBlock(const uint32_t key[4], uint8_t block[kTeaBlock]);
  void teaDecryptBlock(const uint32_t key[4], uint8_t block[kTeaBlock]);

  // CBC over whole blocks. `len` must be a multiple of kTeaBlock.
  void teaCbcEncrypt(const uint8_t key[kTeaKeyBytes], const uint8_t iv[kTeaBlock],
                     uint8_t* data, std::size_t len);
  void teaCbcDecrypt(const uint8_t key[kTeaKeyBytes], const uint8_t iv[kTeaBlock],
                     uint8_t* data, std::size_t len);

  // The way in.
  //
  //   material = SHA256( SHA256(first) || SHA256(second) )
  //
  // and the cipher's key and IV are consecutive slices of that: bytes 0..15
  // are the TEA key, bytes 16..23 the CBC IV. Nothing derived from the two
  // words is ever stored -- there is no digest to compare against, so the
  // only test of a guess is whether the pack it opens makes sense.
  void deriveKey(const std::string& first, const std::string& second,
                 uint8_t keyOut[kTeaKeyBytes], uint8_t ivOut[kTeaBlock]);
}
