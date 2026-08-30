// Copyright (c) 2026 James Leaver.
// SPDX-License-Identifier: MIT

#include "Crypto.h"

#include <cstring>

namespace crypto {
namespace {

  inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

  const uint32_t K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };

  void block(uint32_t h[8], const uint8_t p[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
      w[i] = ((uint32_t)p[i*4] << 24) | ((uint32_t)p[i*4+1] << 16) |
             ((uint32_t)p[i*4+2] << 8) | (uint32_t)p[i*4+3];
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15] >> 3);
      uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
      uint32_t ch = (e & f) ^ (~e & g);
      uint32_t t1 = hh + S1 + ch + K[i] + w[i];
      uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
      uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t t2 = S0 + mj;
      hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
  }
}

void sha256(const uint8_t* data, std::size_t len, uint8_t out[kSha256Bytes]) {
  uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                    0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
  uint8_t buf[64];
  std::size_t i = 0;
  for (; i + 64 <= len; i += 64) block(h, data + i);

  std::size_t rem = len - i;
  std::memcpy(buf, data + i, rem);
  buf[rem++] = 0x80;
  if (rem > 56) { std::memset(buf + rem, 0, 64 - rem); block(h, buf); rem = 0; }
  std::memset(buf + rem, 0, 56 - rem);
  const uint64_t bits = (uint64_t)len * 8;
  for (int b = 0; b < 8; ++b) buf[56 + b] = (uint8_t)(bits >> (56 - 8 * b));
  block(h, buf);

  for (int j = 0; j < 8; ++j) {
    out[j*4]   = (uint8_t)(h[j] >> 24);
    out[j*4+1] = (uint8_t)(h[j] >> 16);
    out[j*4+2] = (uint8_t)(h[j] >> 8);
    out[j*4+3] = (uint8_t)h[j];
  }
}

void sha256(const std::string& s, uint8_t out[kSha256Bytes]) {
  sha256(reinterpret_cast<const uint8_t*>(s.data()), s.size(), out);
}

namespace {
  inline uint32_t rd32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
  }
  inline void wr32(uint8_t* p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
  }
  constexpr uint32_t kDelta = 0x9e3779b9;
}

void teaEncryptBlock(const uint32_t k[4], uint8_t blk[kTeaBlock]) {
  uint32_t v0 = rd32(blk), v1 = rd32(blk + 4), sum = 0;
  for (int i = 0; i < 32; ++i) {
    sum += kDelta;
    v0 += ((v1 << 4) + k[0]) ^ (v1 + sum) ^ ((v1 >> 5) + k[1]);
    v1 += ((v0 << 4) + k[2]) ^ (v0 + sum) ^ ((v0 >> 5) + k[3]);
  }
  wr32(blk, v0); wr32(blk + 4, v1);
}

void teaDecryptBlock(const uint32_t k[4], uint8_t blk[kTeaBlock]) {
  uint32_t v0 = rd32(blk), v1 = rd32(blk + 4), sum = kDelta * 32;
  for (int i = 0; i < 32; ++i) {
    v1 -= ((v0 << 4) + k[2]) ^ (v0 + sum) ^ ((v0 >> 5) + k[3]);
    v0 -= ((v1 << 4) + k[0]) ^ (v1 + sum) ^ ((v1 >> 5) + k[1]);
    sum -= kDelta;
  }
  wr32(blk, v0); wr32(blk + 4, v1);
}

namespace {
  void loadKey(const uint8_t key[kTeaKeyBytes], uint32_t out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = rd32(key + i * 4);
  }
}

void teaCbcEncrypt(const uint8_t key[kTeaKeyBytes], const uint8_t iv[kTeaBlock],
                   uint8_t* data, std::size_t len) {
  uint32_t k[4]; loadKey(key, k);
  uint8_t prev[kTeaBlock];
  std::memcpy(prev, iv, kTeaBlock);
  for (std::size_t off = 0; off + kTeaBlock <= len; off += kTeaBlock) {
    for (std::size_t b = 0; b < kTeaBlock; ++b) data[off + b] ^= prev[b];
    teaEncryptBlock(k, data + off);
    std::memcpy(prev, data + off, kTeaBlock);
  }
}

void teaCbcDecrypt(const uint8_t key[kTeaKeyBytes], const uint8_t iv[kTeaBlock],
                   uint8_t* data, std::size_t len) {
  uint32_t k[4]; loadKey(key, k);
  uint8_t prev[kTeaBlock], cur[kTeaBlock];
  std::memcpy(prev, iv, kTeaBlock);
  for (std::size_t off = 0; off + kTeaBlock <= len; off += kTeaBlock) {
    std::memcpy(cur, data + off, kTeaBlock);
    teaDecryptBlock(k, data + off);
    for (std::size_t b = 0; b < kTeaBlock; ++b) data[off + b] ^= prev[b];
    std::memcpy(prev, cur, kTeaBlock);
  }
}

void deriveKey(const std::string& first, const std::string& second,
               uint8_t keyOut[kTeaKeyBytes], uint8_t ivOut[kTeaBlock]) {
  uint8_t a[kSha256Bytes], b[kSha256Bytes], joined[kSha256Bytes * 2], material[kSha256Bytes];
  sha256(first, a);
  sha256(second, b);
  std::memcpy(joined, a, kSha256Bytes);
  std::memcpy(joined + kSha256Bytes, b, kSha256Bytes);
  sha256(joined, sizeof(joined), material);
  std::memcpy(keyOut, material, kTeaKeyBytes);            // bytes 0..15
  std::memcpy(ivOut,  material + kTeaKeyBytes, kTeaBlock); // bytes 16..23
}
}
