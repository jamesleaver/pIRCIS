// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT
//
// Vectors for the two primitives the content pack is built on. SHA-256 is
// checked against FIPS 180-2 and TEA against its published all-zero vector,
// so a mistake here is caught by arithmetic rather than by the pack simply
// refusing to open.

#include "Crypto.h"
#include <cstdio>
#include <cstring>
#include <string>
static int fails = 0;
static void ck(bool ok, const char* what) {
  std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) ++fails;
}
static std::string hex(const uint8_t* p, size_t n) {
  std::string s; char b[3];
  for (size_t i = 0; i < n; ++i) { std::snprintf(b, 3, "%02x", p[i]); s += b; }
  return s;
}
int main() {
  uint8_t d[32];
  // FIPS 180-2 vectors
  crypto::sha256(std::string(""), d);
  ck(hex(d,32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256(\"\")");
  crypto::sha256(std::string("abc"), d);
  ck(hex(d,32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(\"abc\")");
  crypto::sha256(std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"), d);
  ck(hex(d,32) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "sha256(56-byte message, spans the pad boundary)");
  // a message longer than one block
  std::string big(1000, 'a');
  crypto::sha256(big, d);
  ck(hex(d,32) == "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3", "sha256(1000 x 'a')");

  // TEA: all-zero key and block
  uint32_t k[4] = {0,0,0,0};
  uint8_t blk[8] = {0,0,0,0,0,0,0,0};
  crypto::teaEncryptBlock(k, blk);
  ck(hex(blk,8) == "41ea3a0a94baa940", "TEA encrypts the all-zero block to the published vector");
  crypto::teaDecryptBlock(k, blk);
  ck(hex(blk,8) == "0000000000000000", "TEA decrypt is the exact inverse");

  // Deliberately neutral inputs. The words that actually open the pack appear
  // nowhere in this repository -- not in source, not in a test, not in a
  // comment. The tool that builds the pack takes them as an argument.
  uint8_t key[16], iv[8];
  crypto::deriveKey("alpha", "beta", key, iv);
  uint8_t buf[64], orig[64];
  for (int i = 0; i < 64; ++i) buf[i] = orig[i] = (uint8_t)(i * 7 + 3);
  crypto::teaCbcEncrypt(key, iv, buf, 64);
  ck(std::memcmp(buf, orig, 64) != 0, "CBC actually changes the data");
  crypto::teaCbcDecrypt(key, iv, buf, 64);
  ck(std::memcmp(buf, orig, 64) == 0, "CBC round-trips");
  // Pins the derivation against the same computation done with hashlib, so the
  // firmware and the pack builder cannot drift apart unnoticed.
  ck(hex(key,16) == "8450e9a90d144185def662fffc477da5" &&
     hex(iv,8)   == "e0325d80be5de388",
     "key and IV are the documented slices of SHA256(SHA256(a)||SHA256(b))");

  // the wrong phrase gives a different key
  uint8_t key2[16], iv2[8];
  crypto::deriveKey("alpha", "betb", key2, iv2);
  ck(std::memcmp(key, key2, 16) != 0, "one character different derives a different key");
  crypto::deriveKey("beta", "alpha", key2, iv2);
  ck(std::memcmp(key, key2, 16) != 0, "the two words are not interchangeable");

  std::printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED", fails, fails==1?"":"s");
  return fails ? 1 : 0;
}
