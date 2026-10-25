// Levels (D33): core/adj_levels.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words, its tables from tables[w[1]]. Each channel through its own set's stage, then RGB's (w[11] 0), or
// RGB's first (1); a stage is u = clamp((v - a) k, 0, 1) (0 below a tiny value), its w[12 + s] square roots, then its
// curve, component s of the table (s: 0 Red, 1 Green, 2 Blue, 3 RGB).
import blend;
import gradient;

static const uint kLevelsTiny = 0x30800000, kLevelsTinyRoot = 0x04800000;  // 2^-30, 2^-118: adj_levels.cpp's

// Stage s of value v: adj_levels.cpp's stage().
float levels_stage(uint w[32], StructuredBuffer<float4> tables, uint s, float v) {
    if ((w[10] & (1u << s)) == 0) return v;
    uint roots = w[12 + s];
    precise float d = v - asfloat(w[2 + 2 * s]);
    precise float e = d * asfloat(w[3 + 2 * s]);
    float u = e >= asfloat(roots != 0 ? kLevelsTinyRoot : kLevelsTiny) ? (e < 1 ? e : 1) : 0;  // and NaN, below 0
    for (uint r = 0; r < roots; r++) u = sqrt_rn(u);
    float4 t = gradient_lookup(tables, w[1], u);
    return s == 0 ? t.x : (s == 1 ? t.y : (s == 2 ? t.z : t.w));
}

float3 adj_levels(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    if (w[11] == 0)
        return float3(levels_stage(w, tables, 3, levels_stage(w, tables, 0, c.x)),
                      levels_stage(w, tables, 3, levels_stage(w, tables, 1, c.y)),
                      levels_stage(w, tables, 3, levels_stage(w, tables, 2, c.z)));
    return float3(levels_stage(w, tables, 0, levels_stage(w, tables, 3, c.x)),
                  levels_stage(w, tables, 1, levels_stage(w, tables, 3, c.y)),
                  levels_stage(w, tables, 2, levels_stage(w, tables, 3, c.z)));
}
