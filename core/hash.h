#pragma once
// Noise the CPU and the GPU draw alike: a hash of integers, shaders/lib/hash.slang line by line (integer arithmetic
// modulo 2^32 and a float conversion that is exact, so both give the same bits). Whatever needs randomness per pixel
// draws from it with the pixel's coordinates and a seed: Dissolve (B31, C33), the gradient's dither (gradient.h), Add
// Noise (F37), the brush's Noise (C31). So a preview and its application, the CPU composite and the GPU's, the same
// pixel at another time all get the same value, and nothing keeps random state.
#include <array>
#include <cstdint>

namespace pb {

// Four 32-bit hashes of four integers: pcg4d of Jarzynski and Olano, "Hash Functions for GPU Rendering", Journal of
// Computer Graphics Techniques 9(3), 2020. Every output bit depends on every input bit, and the four outputs are
// independent enough to be four channels' (tests/test_gradient.cpp checks uniformity and correlation over a tile).
// x, y: a pixel (negative coordinates as two's complement, uint32_t(x)); z: a seed (a layer's id, a stroke's, an
// application's); w: a stream, for more values of one pixel and seed. The values are part of the file format in
// effect (a Dissolve layer's pixels): never change them.
inline std::array<uint32_t, 4> hash4(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
  uint32_t v[4] = {x * 1664525u + 1013904223u, y * 1664525u + 1013904223u, z * 1664525u + 1013904223u,
                   w * 1664525u + 1013904223u};
  for (int round = 0; round < 2; round++) {
    if (round == 1)
      for (uint32_t& c : v) c ^= c >> 16;
    v[0] += v[1] * v[3];
    v[1] += v[2] * v[0];
    v[2] += v[0] * v[1];
    v[3] += v[1] * v[2];
  }
  return {v[0], v[1], v[2], v[3]};
}

// One hash of a pixel and a seed: hash4()'s first.
inline uint32_t hash3(uint32_t x, uint32_t y, uint32_t seed) { return hash4(x, y, seed, 0)[0]; }

// A value in [0, 1) from a hash: its top 24 bits over 2^24, a float exactly (the same on the GPU).
inline float hash_unit(uint32_t h) { return float(h >> 8) * (1.f / 16777216); }

}  // namespace pb
