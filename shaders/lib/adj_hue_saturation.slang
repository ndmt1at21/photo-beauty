// Hue/Saturation (D35): core/adj_hue_saturation.cpp's adjust_row() for one pixel, line by line (core/adjust.h says
// how): c, the document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before
// adjust_pixel() clamps; w its pack() words, its ranges from tables[w[1]] (adj_hue_saturation.cpp says which word and
// entry is which). In HSL: the hue turned with max and min kept, the chroma scaled about L = (max + min) / 2, Colorize
// one colour at the pixel's lightness. The helpers serve adj_vibrance.slang too.
import blend;

static const uint kHueSaturationTiny = 0x1f800000;  // 2^-64: adj_hue_saturation.cpp's kTiny

// A channel coming in: 0..1, below 2^-64 0 (and NaN): adj_hue_saturation.cpp's in().
float hue_saturation_in(float v) { return v >= asfloat(kHueSaturationTiny) ? (v < 1 ? v : 1) : 0; }

// n / d to the nearest float, n of either sign (div() takes n >= 0): the CPU's '/'.
float hue_saturation_div(float n, float d) {
    if (n < 0) return -div(-n, d);
    return div(n, d);
}

// The hue of c, max M, chroma C > 0, in sextants: hue6().
float hue_saturation_hue6(float3 c, float M, float C) {
    if (M == c.x) {
        precise float n = c.y - c.z;
        float h = hue_saturation_div(n, C);
        if (h < 0) {
            precise float h6 = h + 6;
            return h6;
        }
        return h;
    }
    if (M == c.y) {
        precise float n = c.z - c.x;
        precise float h = hue_saturation_div(n, C) + 2;
        return h;
    }
    precise float n = c.x - c.y;
    precise float h = hue_saturation_div(n, C) + 4;
    return h;
}

// How much of span s (a, then b, c, d from a, sextants) hue h is in, rb = 1 / b, rd = 1 / (d - c): weight().
float hue_saturation_weight(float4 s, float rb, float rd, float h) {
    precise float t = h - s.x;
    if (t < 0) {
        precise float t6 = t + 6;
        t = t6;
    }
    if (t < s.y) {
        precise float r = t * rb;
        return r;
    }
    if (t <= s.z) return 1;
    if (t < s.w) {
        precise float u = s.w - t;
        precise float r = u * rd;
        return r;
    }
    return 0;
}

// A channel whose part of the chroma is k: part().
float hue_saturation_part(float k, float M, float m, float C) {
    if (k >= 1) return M;
    if (k <= 0) return m;
    precise float v = m + C * k;
    return v;
}

// Colorize: colorize().
float3 hue_saturation_colorize(uint w[32], float3 c, float M, float m) {
    precise float l0 = asfloat(w[6]) * M + asfloat(w[7]) * m + asfloat(w[8]) * c.x + asfloat(w[9]) * c.y +
                       asfloat(w[10]) * c.z;
    float L = l0 > 0 ? (l0 < 1 ? l0 : 1) : 0;
    precise float s2 = L + L;
    precise float den = s2 <= 1 ? s2 : 2 - s2;
    float lt = asfloat(w[5]);
    float L2 = L;
    if (lt > 0) {
        precise float d = 1 - L;
        precise float l2 = L + d * lt;
        L2 = l2;
    } else if (lt < 0) {
        precise float f = 1 + lt;
        precise float l2 = L * f;
        L2 = l2;
    }
    float q;
    if ((w[2] & 2u) != 0) {
        precise float q1 = den * asfloat(w[14]);
        q = q1;
    } else {
        precise float s3 = L2 + L2;
        precise float den2 = s3 <= 1 ? s3 : 2 - s3;
        precise float q2 = asfloat(w[4]) * den2;
        q = q2;
    }
    precise float r = L2 + q * asfloat(w[11]);
    precise float g = L2 + q * asfloat(w[12]);
    precise float b = L2 + q * asfloat(w[13]);
    return float3(r, g, b);
}

float3 adj_hue_saturation(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    float3 v = float3(hue_saturation_in(c.x), hue_saturation_in(c.y), hue_saturation_in(c.z));
    float M = max(max(v.x, v.y), v.z), m = min(min(v.x, v.y), v.z);
    if ((w[2] & 1u) != 0) return hue_saturation_colorize(w, v, M, m);
    precise float C = M - m;
    precise float sum = M + m;
    precise float L = sum * 0.5;
    precise float den = sum <= 1 ? sum : 2 - sum;
    // Master's and the ranges' the pixel's hue is in.
    float H = asfloat(w[4]), St = asfloat(w[5]), Lt = asfloat(w[6]), h = -1;
    uint n = w[3];
    if (n != 0 && C > 0) {
        h = hue_saturation_hue6(v, M, C);
        for (uint j = 0; j < n; j++) {
            float4 s = tables[w[1] + 3 * j], e = tables[w[1] + 3 * j + 1], f = tables[w[1] + 3 * j + 2];
            float wt = hue_saturation_weight(s, e.x, e.y, h);
            precise float H2 = H + wt * e.z;
            precise float S2 = St + wt * e.w;
            precise float L3 = Lt + wt * f.x;
            H = H2;
            St = S2;
            Lt = L3;
        }
        St = St > -1 ? (St < 1 ? St : 1) : -1;
        Lt = Lt > -1 ? (Lt < 1 ? Lt : 1) : -1;
    }
    // The hue turned: max and min stay, the middle channel moves.
    if (C > 0 && H != 0) {
        if (h < 0) h = hue_saturation_hue6(v, M, C);
        precise float t = h + H;
        for (int i = 0; i < 8 && t >= 6; i++) {
            precise float t6 = t - 6;
            t = t6;
        }
        for (int i = 0; i < 8 && t < 0; i++) {
            precise float t6 = t + 6;
            t = t6;
        }
        precise float kr = abs(t - 3) - 1;
        precise float kg = 2 - abs(t - 2);
        precise float kb = 2 - abs(t - 4);
        v = float3(hue_saturation_part(kr, M, m, C), hue_saturation_part(kg, M, m, C), hue_saturation_part(kb, M, m, C));
    }
    // The chroma about L by k (saturation) and g (lightness), L to L2.
    float k = 1;
    if (St < 0) {
        precise float k1 = 1 + St;
        k = k1;
    } else if (St > 0 && C > 0) {
        float top = div(den, C);
        if (St >= 1) {
            k = top;
        } else {
            float up;
            if ((w[2] & 4u) != 0) {
                precise float u = 1 + St;
                up = u;
            } else {
                precise float d1 = 1 - St;
                up = div(1, d1);
            }
            k = top < up ? top : up;
        }
    }
    float L2 = L, g = 1;
    if (Lt > 0) {
        precise float d = 1 - L;
        precise float l2 = L + d * Lt;
        precise float g1 = 1 - Lt;
        L2 = l2;
        g = g1;
    } else if (Lt < 0) {
        precise float g1 = 1 + Lt;
        precise float l2 = L * g1;
        g = g1;
        L2 = l2;
    }
    if ((w[2] & 2u) == 0 && Lt != 0 && C > 0) {
        precise float s2 = L2 + L2;
        precise float den2 = s2 <= 1 ? s2 : 2 - s2;
        g = div(den2, den);
    }
    precise float q = k * g;
    if (q != 1 || L2 != L) {
        precise float r = L2 + (v.x - L) * q;
        precise float gg = L2 + (v.y - L) * q;
        precise float b = L2 + (v.z - L) * q;
        v = float3(r, gg, b);
    }
    return v;
}
