// Brightness/Contrast (D34): core/adj_brightness_contrast.cpp's adjust_row() for one pixel, line by line (core/adjust.h
// says how): c, the document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before
// adjust_pixel() clamps; w its pack() words, its tables from tables[w[1]]. D31's frame: the identity until D34.
float3 adj_brightness_contrast(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    return c;
}
