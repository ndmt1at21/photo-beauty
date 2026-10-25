// Invert (D38): core/adj_invert.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words (none). 1 - v in each channel.
float3 adj_invert(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    precise float3 r = 1 - c;
    return r;
}
