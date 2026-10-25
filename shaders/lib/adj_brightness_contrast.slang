// Brightness/Contrast (D34): core/adj_brightness_contrast.cpp's adjust_row() for one pixel, line by line (core/adjust.h
// says how): c, the document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before
// adjust_pixel() clamps; w its pack() words, its table from tables[w[1]]. Each channel alike: through the curve's
// table (w[2] 1: the default), along a line v w[3] + w[4] (2: Use Legacy), or up to 1 from w[3] (3: Use Legacy at
// Contrast 100); w[2] 0 the identity.
import gradient;

static const uint kBrightnessContrastCurve = 1, kBrightnessContrastLine = 2, kBrightnessContrastStep = 3;
static const uint kBrightnessContrastTiny = 0x1f800000;  // 2^-64: adj_brightness_contrast.cpp's kTiny

// One channel: adj_brightness_contrast.cpp's channel().
float brightness_contrast_channel(uint w[32], StructuredBuffer<float4> tables, float v) {
    float c = v >= asfloat(kBrightnessContrastTiny) ? v : 0;  // and NaN, below 0
    if (w[2] == kBrightnessContrastCurve) return gradient_lookup(tables, w[1], c).x;
    if (w[2] == kBrightnessContrastStep) return c >= asfloat(w[3]) ? 1.0 : 0.0;
    precise float r = c * asfloat(w[3]) + asfloat(w[4]);
    return r;
}

float3 adj_brightness_contrast(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    if (w[2] == 0) return c;
    return float3(brightness_contrast_channel(w, tables, c.x), brightness_contrast_channel(w, tables, c.y),
                  brightness_contrast_channel(w, tables, c.z));
}
