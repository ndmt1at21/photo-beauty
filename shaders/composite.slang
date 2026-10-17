// The GPU composite (core/gpu_composite.cpp): the output tiles of a frame in one dispatch, a thread a pixel, each tile
// a flat list of layers bottom up whose pixels lie in the atlas (or are one value), accumulated in registers as
// premultiplied R, G, B and alpha with blend.slang's formulas, then written once as RGBA U16, alpha not premultiplied:
// composite.cpp's composite_tile() step for step, so the two agree to the bit (blend.slang).
import blend;

struct Consts {
    uint log2;      // kTileLog2: tiles are 2^log2 pixels across
    uint channels;  // of the atlas's tiles: 4 R, G, B, alpha; 2 grey, alpha
    uint u16;       // 1: 16-bit samples, 0: 8-bit
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

// gpu_composite.cpp's Job and Entry.
struct Job {
    uint first, count;  // its layers: entries first .. first + count - 1
    uint out;           // its slot in the output
    uint x, y;          // the canvas's part of it: columns x & 0xffff .. (x >> 16) - 1, rows likewise
};
struct Entry {
    uint slot;     // of its tile in the atlas, kSolid for a solid tile
    uint blend;    // a mode of blend.slang's, P0's only
    float alpha;   // the layer's opacity x fill
    uint pad;
    float4 solid;  // a solid tile's R, G, B, alpha as unit() makes them
};
static const uint kSolid = 0xffffffff;

// Pixel p of the atlas tile in `slot` as R, G, B (grey thrice) and alpha, not premultiplied: composite.cpp's load().
// Samples native-endian (little), packed in 32-bit words.
float4 load(StructuredBuffer<uint> atlas, uint slot, uint p) {
    uint i = (slot << (2 * k.log2)) + p;  // pixel i of the atlas
    if (k.u16 != 0) {
        if (k.channels == 4) {
            uint w0 = atlas[2 * i], w1 = atlas[2 * i + 1];
            return float4(unit16(w0 & 0xffff), unit16(w0 >> 16), unit16(w1 & 0xffff), unit16(w1 >> 16));
        }
        uint w = atlas[i];
        float g = unit16(w & 0xffff);
        return float4(g, g, g, unit16(w >> 16));
    }
    if (k.channels == 4) {
        uint w = atlas[i];
        return float4(unit8(w & 0xff), unit8((w >> 8) & 0xff), unit8((w >> 16) & 0xff), unit8(w >> 24));
    }
    uint w = atlas[i >> 1] >> ((i & 1) * 16);
    float g = unit8(w & 0xff);
    return float4(g, g, g, unit8((w >> 8) & 0xff));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void composite(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<Job> jobs, uniform StructuredBuffer<Entry> entries,
               uniform StructuredBuffer<uint> atlas, uniform RWStructuredBuffer<uint> out) {
    Job j = jobs[id.y];
    uint p = id.x, x = p & ((1u << k.log2) - 1), y = p >> k.log2;
    bool grey = k.channels == 2;  // R, G and B alike: one channel computed for three
    float4 acc = 0;               // premultiplied
    if (x >= (j.x & 0xffff) && x < (j.x >> 16) && y >= (j.y & 0xffff) && y < (j.y >> 16)) {
        for (uint e = j.first; e < j.first + j.count; e++) {
            Entry l = entries[e];
            float4 src = l.slot == kSolid ? l.solid : load(atlas, l.slot, p);
            precise float ab = acc.a, as = src.a * l.alpha;
            acc.r = composite_channel(l.blend, acc.r, ab, src.r, as);
            acc.g = grey ? acc.r : composite_channel(l.blend, acc.g, ab, src.g, as);
            acc.b = grey ? acc.r : composite_channel(l.blend, acc.b, ab, src.b, as);
            acc.a = over_alpha(ab, as);
        }
    }
    uint a = from_unit16(acc.a);
    uint r = a != 0 ? from_unit16(unpremultiply(acc.r, acc.a)) : 0;
    uint g = a != 0 ? (grey ? r : from_unit16(unpremultiply(acc.g, acc.a))) : 0;
    uint b = a != 0 ? (grey ? r : from_unit16(unpremultiply(acc.b, acc.a))) : 0;
    uint o = ((j.out << (2 * k.log2)) + p) * 2;
    out[o] = r | (g << 16);
    out[o + 1] = b | (a << 16);
}
