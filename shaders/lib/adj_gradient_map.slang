// Gradient Map (D38): core/adj_gradient_map.cpp's adjust_row() for one pixel, line by line (core/adjust.h says how): c,
// the document's encoded R, G, B (grey: R = G = B) at pixel (x, y), as the type leaves it before adjust_pixel() clamps;
// w its pack() words: w[2] Reverse, w[3] Dither, w[4] the opacity (the CPU's kUseOpacity), w[5..7] the weights of R,
// G, B (kLumPercent), w[8] the dither's seed, w[9] its width, w[10] its floor; the gradient's table (GradientLut, made
// on the CPU) from tables[w[1]].
import blend;
import gradient;
import hash;

// v dithered by noise n (-0.5..0.5): adj_gradient_map.cpp's dither().
float gradient_map_dither(float v, float n, float wide, float floor16) {
    precise float back = 1 - v;
    float end = v < back ? v : back;
    precise float room = end + end + floor16;
    float wd = end > 0 ? (room < wide ? room : wide) : 0;
    precise float r = v + n * wd;
    return r;
}

float3 adj_gradient_map(uint w[32], StructuredBuffer<float4> tables, float3 c, int x, int y) {
    precise float s = asfloat(w[5]) * c.x + asfloat(w[6]) * c.y + asfloat(w[7]) * c.z;
    float lum = div(s, 100.0);
    precise float back = 1 - lum;
    float t = w[2] != 0 ? back : lum;
    float4 g = gradient_lookup(tables, w[1], t);
    float3 o = g.rgb;
    if (w[4] != 0) {
        precise float3 m = c + (g.rgb - c) * g.a;
        o = m;
    }
    if (w[3] != 0) {
        float4 n = dither_noise(x, y, w[8]);
        float wide = asfloat(w[9]), floor16 = asfloat(w[10]);
        o = float3(gradient_map_dither(o.x, n.x, wide, floor16), gradient_map_dither(o.y, n.y, wide, floor16),
                   gradient_map_dither(o.z, n.z, wide, floor16));
    }
    return o;
}
