// Threshold (D38): core/adj_threshold.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words: w[2] the cut, w[3..5] the weights of R, G, B (kLumPercent). White where the weighted 16-bit levels
// reach the cut, else black.

// v (clamped to 0..1, NaN 0) at the nearest of 65536 levels: adj_threshold.cpp's level16().
uint threshold_level16(float v) {
    float c = v > 0 ? (v < 1 ? v : 1) : 0;
    precise float x = c * 65535.0;
    precise float r = x + 0.5;
    return uint(r);
}

float3 adj_threshold(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    uint l = w[3] * threshold_level16(c.x) + w[4] * threshold_level16(c.y) + w[5] * threshold_level16(c.z);
    float v = l >= w[2] ? 1.0 : 0.0;
    return float3(v, v, v);
}
