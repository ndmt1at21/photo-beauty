// What tests/test_gradient.cpp compares with the CPU, a thread a pixel of a rectangle `width` pixels wide from (x0, y0):
// gradient.slang's functions as a kernel using them would call them. Modes:
//   0: the gradient's samples, gradient_samples() (gradient_pixels() into RGBA): R | G << 16, B | alpha << 16;
//   1: t, gradient_t()'s float bits;
//   2: hash4(x, y, seed, w), four words;
//   3: the table at t = asfloat(ts[p]) (Gradient Map's way: t from the pixel, not the shape), dithered at the pixel
//      when asked, as mode 0.
import gradient;
import hash;

struct Consts {
    uint mode;
    int x0, y0;
    uint width;     // pixels a row
    uint count;     // pixels
    uint u16;       // 16-bit samples, else 8
    uint dither;    // modes 0 and 3
    uint seed;      // the dither's, the hash's
    uint w;         // mode 2: the stream
    uint style, reverse;  // the shape: gradient.h's GradientPlane
    float sx, sy, ax, ay, inv_len;
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

[shader("compute")]
[numthreads(64, 1, 1)]
void gradient_check(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<float4> lut,
                    uniform StructuredBuffer<uint> ts, uniform RWStructuredBuffer<uint> out) {
    uint p = id.x;
    if (p >= k.count) return;
    int x = k.x0 + int(p % k.width), y = k.y0 + int(p / k.width);
    GradientPlane g;
    g.style = k.style;
    g.reverse = k.reverse;
    g.sx = k.sx;
    g.sy = k.sy;
    g.ax = k.ax;
    g.ay = k.ay;
    g.inv_len = k.inv_len;
    g.pad = 0;
    if (k.mode == 1) {
        out[p] = asuint(gradient_t(g, x, y));
        return;
    }
    if (k.mode == 2) {
        uint4 h = hash4(uint4(uint(x), uint(y), k.seed, k.w));
        out[4 * p] = h.x;
        out[4 * p + 1] = h.y;
        out[4 * p + 2] = h.z;
        out[4 * p + 3] = h.w;
        return;
    }
    uint4 s;
    if (k.mode == 3) {
        float4 c = gradient_lookup(lut, 0, asfloat(ts[p]));
        float4 n = k.dither != 0 ? dither_noise(x, y, k.seed) : float4(0, 0, 0, 0);
        s = uint4(to_level(c.r, k.u16, n.x), to_level(c.g, k.u16, n.y), to_level(c.b, k.u16, n.z),
                  to_level(c.a, k.u16, n.w));
    } else {
        s = gradient_samples(lut, 0, g, x, y, k.u16, k.dither, k.seed);
    }
    out[2 * p] = s.x | (s.y << 16);
    out[2 * p + 1] = s.z | (s.w << 16);
}
