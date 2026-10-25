// core/adjust.h's adjustments for kernels (D40's composite, adjust_apply.slang): adjust_pixel() of an adjustment's
// pack() words and tables, each type's function from its own module (shaders/lib/adj_<type>.slang, D33-D39), so a
// kernel gives Adjustment::apply()'s bits (tests/adjust_check.h). A kernel that adjusts: `import adjust;`, and blend,
// gradient or hash for its own calls (Slang's imports are not transitive). The switch below is the table of types:
// nothing here changes when a type's body does.
import gradient;
import adj_brightness_contrast;
import adj_levels;
import adj_curves;
import adj_exposure;
import adj_vibrance;
import adj_hue_saturation;
import adj_color_balance;
import adj_black_white;
import adj_photo_filter;
import adj_channel_mixer;
import adj_color_lookup;
import adj_invert;
import adj_posterize;
import adj_threshold;
import adj_gradient_map;
import adj_selective_color;

// adjust.h's AdjustParams, 128 bytes: w[0] the type (AdjType's value), w[1] where its tables start in `tables`
// (float4s), w[2..31] the type's (floats as their bits: asfloat()).
struct AdjustParams {
    uint w[32];
};

// AdjType's values.
static const uint kAdjBrightnessContrast = 0, kAdjLevels = 1, kAdjCurves = 2, kAdjExposure = 3, kAdjVibrance = 4,
                  kAdjHueSaturation = 5, kAdjColorBalance = 6, kAdjBlackWhite = 7, kAdjPhotoFilter = 8,
                  kAdjChannelMixer = 9, kAdjColorLookup = 10, kAdjInvert = 11, kAdjPosterize = 12, kAdjThreshold = 13,
                  kAdjGradientMap = 14, kAdjSelectiveColor = 15, kAdjTestMatrix = 16;

// 0..1, NaN 0: apply()'s last step.
float adjust_clamp(float v) { return v > 0 ? (v < 1 ? v : 1) : 0; }

// adjust.cpp's TestMatrix (the tests'), line by line: each channel out of a 3 x 4 matrix (w[2..13], rows R, G, B),
// then to its power through the curves' table when w[14] is 1 (R, G, B in x, y, z).
float3 adj_test_matrix(uint w[32], StructuredBuffer<float4> tables, float3 c) {
    precise float r = asfloat(w[2]) * c.x + asfloat(w[3]) * c.y + asfloat(w[4]) * c.z + asfloat(w[5]);
    precise float g = asfloat(w[6]) * c.x + asfloat(w[7]) * c.y + asfloat(w[8]) * c.z + asfloat(w[9]);
    precise float b = asfloat(w[10]) * c.x + asfloat(w[11]) * c.y + asfloat(w[12]) * c.z + asfloat(w[13]);
    if (w[14] != 0) {
        r = gradient_lookup(tables, w[1], r).x;
        g = gradient_lookup(tables, w[1], g).y;
        b = gradient_lookup(tables, w[1], b).z;
    }
    return float3(r, g, b);
}

// c (R, G, B, the document's encoded values 0..1; a grey document's R = G = B, R the result) as the adjustment leaves
// it at pixel (x, y) of the level, clamped to 0..1: Adjustment::apply(). Alpha is the caller's: adjustments keep it.
float3 adjust_pixel(AdjustParams p, StructuredBuffer<float4> tables, float3 c, int x, int y) {
    float3 r = c;
    switch (p.w[0]) {
    case kAdjBrightnessContrast: r = adj_brightness_contrast(p.w, tables, c, x, y); break;
    case kAdjLevels: r = adj_levels(p.w, tables, c, x, y); break;
    case kAdjCurves: r = adj_curves(p.w, tables, c, x, y); break;
    case kAdjExposure: r = adj_exposure(p.w, tables, c, x, y); break;
    case kAdjVibrance: r = adj_vibrance(p.w, tables, c, x, y); break;
    case kAdjHueSaturation: r = adj_hue_saturation(p.w, tables, c, x, y); break;
    case kAdjColorBalance: r = adj_color_balance(p.w, tables, c, x, y); break;
    case kAdjBlackWhite: r = adj_black_white(p.w, tables, c, x, y); break;
    case kAdjPhotoFilter: r = adj_photo_filter(p.w, tables, c, x, y); break;
    case kAdjChannelMixer: r = adj_channel_mixer(p.w, tables, c, x, y); break;
    case kAdjColorLookup: r = adj_color_lookup(p.w, tables, c, x, y); break;
    case kAdjInvert: r = adj_invert(p.w, tables, c, x, y); break;
    case kAdjPosterize: r = adj_posterize(p.w, tables, c, x, y); break;
    case kAdjThreshold: r = adj_threshold(p.w, tables, c, x, y); break;
    case kAdjGradientMap: r = adj_gradient_map(p.w, tables, c, x, y); break;
    case kAdjSelectiveColor: r = adj_selective_color(p.w, tables, c, x, y); break;
    case kAdjTestMatrix: r = adj_test_matrix(p.w, tables, c); break;
    default: break;
    }
    return float3(adjust_clamp(r.x), adjust_clamp(r.y), adjust_clamp(r.z));
}
