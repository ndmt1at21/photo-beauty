// core/blend.h line by line for the GPU composite (composite.slang): the same formulas in the same order on the same
// floats, and rounded the same way, so the GPU composite gives composite_tile()'s pixels to the bit where both follow
// IEEE single precision (x86-64 CPUs; ARM ones fuse multiply-adds unless built with -ffp-contract=off). The Color Burn
// edge needs it: by a source near black over a backdrop near white, an ulp of the backdrop moves the result by up to
// 1/Cs, hundreds of 16-bit levels, so the backdrop has to come out of the layers below it bit for bit.
//   - Every operation is `precise` (SPIR-V NoContraction): no fused multiply-add, whose single rounding the CPU lacks.
//   - Vulkan allows 2.5 ulp for a division and more for a square root: div() and sqrt_rn() put the GPU's result right
//     in integers, to the correctly rounded one the CPU's '/' and std::sqrt give.
//   - Samples become floats as composite.cpp's unit() makes them, v / max rounded to the nearest float, without float64
//     (the Iris Xe has none) and without a division: unit16().
// Subnormals may flush to 0 on the GPU (Vulkan's default); the composite meets them only as weights far below a
// 16-bit level.

// Blend's values (core/doc.h) for the modes P0 composites; the composite maps the others to Normal, as composite.cpp.
static const uint kNormal = 1, kDarken = 3, kMultiply = 4, kColorBurn = 5, kLighten = 8, kScreen = 9, kColorDodge = 10,
                  kOverlay = 13, kSoftLight = 14, kLinearLight = 17, kDifference = 20;

// v / 65535 rounded to the nearest float. In binary v / 65535 is v's 16 bits repeated without end, so its 24
// significant bits and the next one are those of two copies shifted to v's first 1 bit, and it is never a tie.
float unit16(uint v) {
    if (v == 0) return 0;
    if (v >= 65535) return 1;
    uint lz = 15 - firstbithigh(v);  // v's leading zeros in 16 bits
    uint w = ((v << 16) | v) << lz;  // the 32 bits of v / 65535 from its first 1 bit
    // 2^-(lz + 1) x the 24 bits w >> 8 over 2^23, rounded by the bit after them; a carry into 2^24 bumps the exponent
    return asfloat(((125 - lz) << 23) + (w >> 8) + ((w >> 7) & 1));
}
float unit8(uint v) { return unit16(v * 257); }  // v / 255 = 257 v / 65535, the same float

// n / d rounded to the nearest float, for n >= 0, d > 0 normal and a normal quotient, as the composite divides (else the
// GPU's quotient as it is). The GPU's quotient q is within a few ulp; with N / D (24-bit significands) in [1, 2) and
// M = q's significand at the quotient's exponent e, the remainder R = N 2^23 - M D is small, so exact in 32 bits
// modulo 2^32: step M until 0 <= R < D, then round up when the rest is past half (never exactly half: no quotient of
// two floats lies midway between two).
float div(float n, float d) {
    precise float q = n / d;
    uint un = asuint(n), ud = asuint(d);
    uint N = (un & 0x7fffff) | 0x800000, D = (ud & 0x7fffff) | 0x800000;
    int e = int(un >> 23) - int(ud >> 23);
    if (N < D) {
        N <<= 1;
        e--;
    }
    if (!(n > 0) || (un >> 23) == 0 || (ud >> 23) == 0 || e < -104 || e > 126) return q;  // 0, subnormals
    precise float scaled = q * asfloat(uint(150 - e) << 23);  // q 2^(23 - e), exactly: about N 2^23 / D
    uint M = uint(scaled);
    int R = int((N << 23) - M * D);
    for (int i = 0; i < 16 && R < 0; i++) {
        M--;
        R += int(D);
    }
    for (int i = 0; i < 16 && R >= int(D); i++) {
        M++;
        R -= int(D);
    }
    if (2 * R > int(D)) M++;
    return asfloat((uint(e + 126) << 23) + M);  // M 2^(e - 23); M = 2^24 carries into the exponent
}

// sqrt(x) rounded to the nearest float, for x >= 0 (Soft Light's Cb). x = X 2^k with X its 24-bit
// significand; T = X 2^(24 - k mod 2) has sqrt(T) in [2^23, 2^24) and sqrt(x) = sqrt(T) 2^p: from M, the GPU's root at
// that scale, R = T - M^2 is small, so exact modulo 2^32: step M until M^2 <= T < (M + 1)^2, then round up when
// T > (M + 1/2)^2, that is R > M (never a tie in integers).
float sqrt_rn(float x) {
    precise float s = sqrt(x);
    uint ux = asuint(x);
    if (!(x > 0) || (ux >> 23) == 0) return s;  // 0, and subnormals
    int k = int(ux >> 23) - 150;
    uint odd = uint(k) & 1, X = (ux & 0x7fffff) | 0x800000;
    int p = (k - 24 + int(odd)) / 2;  // even, so exact
    uint T = X << (24 - odd);         // modulo 2^32
    precise float scaled = s * asfloat(uint(127 - p) << 23);  // s 2^-p, exactly
    uint M = uint(scaled);
    int R = int(T - M * M);
    for (int i = 0; i < 16 && R < 0; i++) {
        M--;
        R += int(2 * M + 1);
    }
    for (int i = 0; i < 16 && R > int(2 * M); i++) {
        R -= int(2 * M + 1);
        M++;
    }
    if (R > int(M)) M++;
    return asfloat((uint(p + 149) << 23) + M);
}

// B(Cb, Cs) of the separable modes.
float multiply(float cb, float cs) {
    precise float r = cb * cs;
    return r;
}
float screen(float cb, float cs) {
    precise float r = cb + cs - cb * cs;
    return r;
}
float overlay(float cb, float cs) {
    precise float c2 = 2 * cb, c21 = c2 - 1;
    return cb <= 0.5 ? multiply(cs, c2) : screen(cs, c21);
}
float darken(float cb, float cs) { return cb < cs ? cb : cs; }
float lighten(float cb, float cs) { return cb > cs ? cb : cs; }
float difference(float cb, float cs) {
    precise float d = cb - cs;
    return abs(d);
}
float color_dodge(float cb, float cs) {
    if (cb <= 0) return 0;
    if (cs >= 1) return 1;
    precise float v = div(cb, 1 - cs);
    return v < 1 ? v : 1;
}
float color_burn(float cb, float cs) {
    if (cb >= 1) return 1;
    if (cs <= 0) return 0;
    precise float v = div(1 - cb, cs);
    precise float r = 1 - v;
    return v < 1 ? r : 0;
}
float soft_light(float cb, float cs) {
    if (cs <= 0.5) {
        precise float r = cb - (1 - 2 * cs) * cb * (1 - cb);
        return r;
    }
    precise float root = sqrt_rn(cb);
    precise float r = cb + (2 * cs - 1) * (root - cb);
    return r;
}
float linear_light(float cb, float cs) {
    precise float v = cb + 2 * cs - 1;
    return v < 0 ? 0 : v < 1 ? v : 1;
}

float separable(uint mode, float cb, float cs) {
    switch (mode) {
    case kDarken: return darken(cb, cs);
    case kMultiply: return multiply(cb, cs);
    case kColorBurn: return color_burn(cb, cs);
    case kLighten: return lighten(cb, cs);
    case kScreen: return screen(cb, cs);
    case kColorDodge: return color_dodge(cb, cs);
    case kOverlay: return overlay(cb, cs);
    case kSoftLight: return soft_light(cb, cs);
    case kLinearLight: return linear_light(cb, cs);
    case kDifference: return difference(cb, cs);
    default: return cs;  // Normal
    }
}

// Over an opaque backdrop (a photo's, as most pixels are) a == 1: the CPU's c_p / a is c_p exactly, which div()
// gives too, after its steps.
float unpremultiply(float c_p, float a) {
    if (a == 1) return c_p < 1 ? c_p : 1;
    return a > 0 ? (c_p < a ? div(c_p, a) : 1) : 0;
}

float over(float cb_p, float ab, float cs, float as, float b) {
    precise float r = (1 - as) * cb_p + as * ((1 - ab) * cs + ab * b);
    return r;
}
float over_alpha(float ab, float as) {
    precise float r = as + ab * (1 - as);
    return r;
}
// blend.h's composite() (the kernel's entry point has its name).
float composite_channel(uint mode, float cb_p, float ab, float cs, float as) {
    if (mode == kNormal) {
        precise float r = (1 - as) * cb_p + as * cs;
        return r;
    }
    return over(cb_p, ab, cs, as, separable(mode, unpremultiply(cb_p, ab), cs));
}

// blend.h's mix(): a Pass Through group below full opacity o, a channel premultiplied (mix is a GLSL intrinsic's name).
float mix_group(float before, float after, float o) {
    precise float r = (1 - o) * before + o * after;
    return r;
}

// The stroke over its layer's pixel, k = its opacity x coverage there.
float paint_alpha(float a, float k) {
    precise float r = k + a * (1 - k);
    return r;
}
float paint(float c, float color, float k, float a_out) {
    if (!(k > 0)) return c;
    precise float r = c + (color - c) * div(k, a_out);
    return r;
}
float erase_alpha(float a, float k) {
    precise float r = a * (1 - k);
    return r;
}

// tile.h's from_unit() into 16 bits: the nearest level, clamped.
uint from_unit16(float f) {
    precise float v = f * 65535 + 0.5;
    return f > 0 ? (f < 1 ? uint(v) : 65535) : 0;
}
