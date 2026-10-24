// core/hash.h line by line: noise the CPU and the GPU draw alike, a hash of a pixel's coordinates and a seed. Integer
// arithmetic modulo 2^32, then a float conversion that is exact, so both sides give the same bits.

// pcg4d of Jarzynski and Olano, "Hash Functions for GPU Rendering" (JCGT 9(3), 2020): four 32-bit hashes of x, y (a
// pixel, negative ones as uint(x)), z (a seed) and w (a stream).
uint4 hash4(uint4 v) {
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    v ^= v >> 16u;
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

// One hash of a pixel and a seed: hash4()'s first.
uint hash3(uint x, uint y, uint seed) { return hash4(uint4(x, y, seed, 0)).x; }

// A value in [0, 1): the hash's top 24 bits over 2^24, exactly.
float hash_unit(uint h) {
    precise float u = float(h >> 8) * (1.0 / 16777216.0);
    return u;
}
