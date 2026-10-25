// Exposure (D34): core/adj_exposure.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words, its table from tables[w[1]]. Channel ch through component ch of the table at sqrt(u / 2) up to u
// 0.5 and 1 - sqrt((1 - u) / 2) past it, u = (v - w[3 + ch]) w[6 + ch] clamped to 0..1; w[2] 0 the identity.
import blend;
import gradient;

static const uint kExposureTiny = 0x1f800000;  // 2^-64: adj_exposure.cpp's kTiny

// Channel ch's place along its table: adj_exposure.cpp's along().
float exposure_along(uint w[32], uint ch, float v) {
    float c = v >= asfloat(kExposureTiny) ? v : 0;  // and NaN, below 0
    precise float d = c - asfloat(w[3 + ch]);
    precise float e = d * asfloat(w[6 + ch]);
    float u = e > 0 ? (e < 1 ? e : 1) : 0;
    if (u <= 0.5) {
        precise float h = u * 0.5;
        return sqrt_rn(h);
    }
    precise float h = (1 - u) * 0.5;
    precise float t = 1 - sqrt_rn(h);
    return t;
}

float3 adj_exposure(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    if (w[2] == 0) return c;
    return float3(gradient_lookup(tables, w[1], exposure_along(w, 0, c.x)).x,
                  gradient_lookup(tables, w[1], exposure_along(w, 1, c.y)).y,
                  gradient_lookup(tables, w[1], exposure_along(w, 2, c.z)).z);
}
