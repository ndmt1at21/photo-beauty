// Vibrance (D35): core/adj_vibrance.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c, the
// document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps; w
// its pack() words (adj_vibrance.cpp says which is which). The chroma scaled about L = (max + min) / 2 by
// 1 + vibrance (1 - S) w, w less on skin tones, times 1 + saturation; HSL's helpers are adj_hue_saturation.slang's.
import blend;
import adj_hue_saturation;

float3 adj_vibrance(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    float3 v = float3(hue_saturation_in(c.x), hue_saturation_in(c.y), hue_saturation_in(c.z));
    float M = max(max(v.x, v.y), v.z), m = min(min(v.x, v.y), v.z);
    precise float C = M - m;
    if (!(C > 0)) return v;
    precise float sum = M + m;
    precise float L = sum * 0.5;
    precise float den = sum <= 1 ? sum : 2 - sum;
    float S = div(C, den);
    if (S > 1) S = 1;
    precise float a = 1 - S;
    float vib = asfloat(w[2]), protect = asfloat(w[10]);
    if (vib != 0 && protect != 0) {
        float4 skin = float4(asfloat(w[4]), asfloat(w[5]), asfloat(w[6]), asfloat(w[7]));
        float wt = hue_saturation_weight(skin, asfloat(w[8]), asfloat(w[9]), hue_saturation_hue6(v, M, C));
        precise float pw = protect * wt;
        precise float one = 1 - pw;
        precise float a2 = a * one;
        a = a2;
    }
    precise float f = 1 + vib * a;
    precise float q = f * asfloat(w[3]);
    if (q != 1) {
        precise float r = L + (v.x - L) * q;
        precise float g = L + (v.y - L) * q;
        precise float b = L + (v.z - L) * q;
        return float3(r, g, b);
    }
    return v;
}
