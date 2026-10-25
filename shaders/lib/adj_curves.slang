// Curves (D33): core/adj_curves.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words, its tables from tables[w[1]]. Each channel through its own curve's stage, then RGB's (w[11] 0), or
// RGB's first (1); a stage is the table at t = clamp((v - a) k, 0, 1) (0 below a tiny value), component s (s: 0 Red,
// 1 Green, 2 Blue, 3 RGB).
import gradient;

static const uint kCurvesTiny = 0x30800000;  // 2^-30: adj_curves.cpp's kTiny

// Stage s of value v: adj_curves.cpp's stage().
float curves_stage(uint w[32], StructuredBuffer<float4> tables, uint s, float v) {
    if ((w[10] & (1u << s)) == 0) return v;
    precise float d = v - asfloat(w[2 + 2 * s]);
    precise float e = d * asfloat(w[3 + 2 * s]);
    float u = e >= asfloat(kCurvesTiny) ? (e < 1 ? e : 1) : 0;  // and NaN, below 0
    float4 t = gradient_lookup(tables, w[1], u);
    return s == 0 ? t.x : (s == 1 ? t.y : (s == 2 ? t.z : t.w));
}

float3 adj_curves(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    if (w[11] == 0)
        return float3(curves_stage(w, tables, 3, curves_stage(w, tables, 0, c.x)),
                      curves_stage(w, tables, 3, curves_stage(w, tables, 1, c.y)),
                      curves_stage(w, tables, 3, curves_stage(w, tables, 2, c.z)));
    return float3(curves_stage(w, tables, 0, curves_stage(w, tables, 3, c.x)),
                  curves_stage(w, tables, 1, curves_stage(w, tables, 3, c.y)),
                  curves_stage(w, tables, 2, curves_stage(w, tables, 3, c.z)));
}
