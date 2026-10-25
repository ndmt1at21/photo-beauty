// Posterize (D38): core/adj_posterize.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words: w[2] the levels n, w[3] 1 for the nearest level (the CPU's kNearestLevel).
import blend;

// v (clamped to 0..1, NaN 0) at the nearest of 65536 levels: adj_posterize.cpp's level16().
uint posterize_level16(float v) {
    float c = v > 0 ? (v < 1 ? v : 1) : 0;
    precise float x = c * 65535.0;
    precise float r = x + 0.5;
    return uint(r);
}

// v at its level of n, k / (n - 1): adj_posterize.cpp's posterize().
float posterize_value(float v, uint n, uint nearest) {
    uint s = posterize_level16(v), top = n - 1;
    uint k = nearest != 0 ? (2 * s * top + 65535) / 131070 : s * n / 65535;
    uint l = k < top ? k : top;
    return div(float(l), float(top));
}

float3 adj_posterize(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    return float3(posterize_value(c.x, w[2], w[3]), posterize_value(c.y, w[2], w[3]), posterize_value(c.z, w[2], w[3]));
}
