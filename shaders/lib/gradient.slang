// core/gradient.h's shape, table lookup and dither for kernels (D40's fill layers, L34's Gradient Overlay, D38's
// Gradient Map): gradient.cpp's gradient_t(), GradientLut::at(), dither_noise() and to_level() line by line, every
// float operation `precise` and in the CPU's order, division and square root rounded as the CPU's (blend.slang's div(),
// sqrt_rn()), so a kernel gives render_gradient()'s samples to the bit (tests/test_gradient.cpp, gradient_check.slang).
// The table itself (GradientLut, 4098 float4) is made on the CPU and uploaded.
import blend;
import hash;

// GradientStyle's values.
static const uint kGradientLinear = 0, kGradientRadial = 1, kGradientAngle = 2, kGradientReflected = 3,
                  kGradientDiamond = 4;
static const uint kGradientSteps = 4096;  // GradientLut::kSteps: entry i is t = i / 4096

// gradient.h's GradientPlane, 32 bytes.
struct GradientPlane {
    uint style;
    uint reverse;      // 1: 1 - t
    float sx, sy;      // the start
    float ax, ay;      // (end - start) / |end - start|²
    float inv_len;     // 1 / |end - start|
    uint pad;
};

// Abramowitz and Stegun 4.4.49's coefficients and 1 / 2π, gradient.cpp's float bits.
static const uint kAtan[9] = { 0x3f800000, 0xbeaaaa6c, 0x3e4cbbe5, 0xbe117fc7, 0x3dda3d83,
                               0xbd9a3174, 0x3d2fc1fe, 0xbc846e02, 0x3b3bd74a };
static const uint kTurn = 0x3e22f983;

// The turn from the u axis to (u, v), 0..1: gradient.cpp's turns().
float gradient_turns(float u, float v) {
    float au = abs(u), av = abs(v);
    float hi = au > av ? au : av, lo = au > av ? av : au;
    if (!(hi > 0)) return 0;
    precise float r = div(lo, hi);
    precise float s = r * r;
    float p = asfloat(kAtan[8]);
    for (int k = 7; k >= 0; k--) {
        precise float q = asfloat(kAtan[k]) + s * p;
        p = q;
    }
    precise float a = r * p * asfloat(kTurn);
    precise float b = 0.25 - a;
    float c = av > au ? b : a;
    precise float d = 0.5 - c;
    float e = u < 0 ? d : c;
    precise float f = 1 - e;
    return v < 0 ? f : e;
}

// t at pixel (x, y)'s centre, 0..1: gradient.cpp's gradient_t().
float gradient_t(GradientPlane g, int x, int y) {
    precise float dx = (float(x) + 0.5) - g.sx;
    precise float dy = (float(y) + 0.5) - g.sy;
    precise float u = dx * g.ax + dy * g.ay;
    precise float t = u;
    if (g.style == kGradientRadial) {
        precise float d2 = dx * dx + dy * dy;
        precise float r = sqrt_rn(d2) * g.inv_len;
        t = r;
    } else if (g.style == kGradientAngle) {
        precise float v = dx * g.ay - dy * g.ax;
        t = gradient_turns(u, v);
    } else if (g.style == kGradientReflected) {
        t = abs(u);
    } else if (g.style == kGradientDiamond) {
        precise float v = dx * g.ay - dy * g.ax;
        precise float d = abs(u) + abs(v);
        t = d;
    }
    t = t > 0 ? (t < 1 ? t : 1) : 0;  // NaN: 0
    precise float rev = 1 - t;
    return g.reverse != 0 ? rev : t;
}

// The table's entry i at or below t (clamped to 0..1, NaN 0) and how far f t lies on to entry i + 1.
void gradient_entry(float t, out uint i, out float f) {
    float c = t > 0 ? (t < 1 ? t : 1) : 0;
    precise float x = c * float(kGradientSteps);
    i = uint(x);
    precise float r = x - float(i);
    f = r;
}
// Between two entries, f of the way.
float4 gradient_mix(float4 a, float4 b, float f) {
    precise float4 r = a + (b - a) * f;
    return r;
}
// The gradient at t from a table at lut[base]: GradientLut::at().
float4 gradient_lookup(StructuredBuffer<float4> lut, uint base, float t) {
    uint i;
    float f;
    gradient_entry(t, i, f);
    return gradient_mix(lut[base + i], lut[base + i + 1], f);
}

// The dither's noise at pixel (x, y), [-0.5, 0.5) for R, G, B, alpha: dither_noise().
float4 dither_noise(int x, int y, uint seed) {
    uint4 h = hash4(uint4(uint(x), uint(y), seed, 0));
    precise float4 n = float4(hash_unit(h.x), hash_unit(h.y), hash_unit(h.z), hash_unit(h.w)) - 0.5;
    return n;
}

// v (0..1) as a sample of the depth (u16: 16 bits, else 8), dithered by `noise` unless it is 0: to_level(), from_unit()
// of tile.h undithered.
uint to_level(float v, uint u16, float noise) {
    float m = u16 != 0 ? 65535.0 : 255.0, amp = u16 != 0 ? 257.0 : 1.0;
    precise float x = v * m;
    precise float back = m - x;
    float end = x < back ? x : back;
    precise float room = end + end + 1;
    float w = room < amp ? room : amp;
    precise float yd = x + noise * w;
    float y = noise != 0 ? yd : x;
    precise float r = y + 0.5;
    return y > 0 ? (y < m ? uint(r) : uint(m)) : 0;
}

// The gradient's samples at pixel (x, y) from a table at lut[base]: R, G, B, alpha as gradient_pixels() writes them
// into RGBA (a grey table: R = G = B), dithered when `dither` is not 0. A fill layer's or an overlay's source pixel is
// these as unit16() or unit8() makes them, so that it composites as render_gradient()'s tiles would.
uint4 gradient_samples(StructuredBuffer<float4> lut, uint base, GradientPlane g, int x, int y, uint u16, uint dither,
                       uint seed) {
    float4 c = gradient_lookup(lut, base, gradient_t(g, x, y));
    float4 n = dither != 0 ? dither_noise(x, y, seed) : float4(0, 0, 0, 0);
    return uint4(to_level(c.r, u16, n.x), to_level(c.g, u16, n.y), to_level(c.b, u16, n.z), to_level(c.a, u16, n.w));
}
