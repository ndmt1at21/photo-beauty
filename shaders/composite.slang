// The GPU composite (core/gpu_composite.cpp): the output tiles of a frame in one dispatch, a thread a pixel, each tile
// a list of steps bottom up (composite_plan()'s, composite.h) whose layers' pixels lie in the atlas (or are one value),
// accumulated in registers as premultiplied R, G, B and alpha with blend.slang's formulas, then written once as RGBA
// U16, alpha not premultiplied: composite.cpp's composite_tile() step for step, so the two agree to the bit
// (blend.slang). A group pushes the accumulator and starts another (isolated: on transparency; Pass Through: on a copy),
// popped back into it as a layer or by mixing. The accumulators open at the stroke's layer can go to `below` (kSave)
// for later frames to start from there (kLoad), so that while painting a frame composites only the layer and those
// above it.
import blend;

static const uint kMaxDepth = 10;  // groups open at once: gpu_composite.cpp's; Photoshop nests 10
// The accumulators the groups open pushed, bottom first: per invocation, never zeroed (a plan pops what it pushed),
// so that a tile without groups never touches it.
static float4 open[kMaxDepth];

struct Consts {
    uint log2;      // kTileLog2: tiles are 2^log2 pixels across
    uint channels;  // of the atlas's tiles: 4 R, G, B, alpha; 2 grey, alpha
    uint u16;       // 1: 16-bit samples, 0: 8-bit
    uint erase;     // the stroke takes alpha away, else lays its colour
    float red, green, blue, opacity;  // the stroke's (grey: red thrice)
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

// gpu_composite.cpp's Job and Entry.
struct Job {
    uint first, count;  // its steps: entries first .. first + count - 1
    uint out;           // its slot in the output
    uint x, y;          // the canvas's part of it: columns x & 0xffff .. (x >> 16) - 1, rows likewise
};
struct Entry {
    uint op;           // kLayer, kPush, kPop (either | kPass), kSave, kLoad
    uint slot;         // kLayer: its tile's slot in the atlas, kSolid for `solid`; kSave, kLoad: a slot of `below`
    uint blend;        // kLayer, kPop: a mode of blend.slang's, P0's only
    float alpha;       // kLayer, kPop: opacity x fill
    float4 solid;      // kLayer: a solid tile's R, G, B, alpha as unit() makes them
    uint cover;        // kLayer: the stroke's coverage here: its tile's slot in the atlas, kSolid for cover_solid, kNone
    float cover_solid;
    uint level;        // kSave, kLoad: which accumulator, 0 the bottom one
    uint pad;
};
static const uint kSolid = 0xffffffff, kNone = 0xfffffffe;
static const uint kLayer = 0, kPush = 1, kPop = 2, kSave = 3, kLoad = 4, kPass = 8;

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

// Pixel p of the stroke buffer's tile in `slot`: 1-channel U16, two samples a word from the slot's start (a slot holds
// a tile of the document's format, at least as large).
float coverage(StructuredBuffer<uint> atlas, uint slot, uint p) {
    uint words = (k.channels << (2 * k.log2 - 2)) << k.u16;  // a slot's
    uint w = atlas[slot * words + (p >> 1)];
    return unit16((w >> ((p & 1) * 16)) & 0xffff);
}

// src (not premultiplied) over acc in `mode` at `alpha`: composite.cpp's over_row(); grey: one channel for three.
float4 over(float4 acc, float4 src, uint mode, float alpha, bool grey) {
    precise float ab = acc.a, as = src.a * alpha;
    float4 r;
    r.r = composite_channel(mode, acc.r, ab, src.r, as);
    r.g = grey ? r.r : composite_channel(mode, acc.g, ab, src.g, as);
    r.b = grey ? r.r : composite_channel(mode, acc.b, ab, src.b, as);
    r.a = over_alpha(ab, as);
    return r;
}

// The stroke over its layer's pixel px, `cover` its coverage there: composite.cpp's apply_stroke().
float4 stroke(float4 px, float cover) {
    precise float s = k.opacity * cover;
    if (k.erase != 0) {
        px.a = erase_alpha(px.a, s);
        return px;
    }
    float a = paint_alpha(px.a, s);
    return float4(paint(px.r, k.red, s, a), paint(px.g, k.green, s, a), paint(px.b, k.blue, s, a), a);
}

[shader("compute")]
[numthreads(64, 1, 1)]
void composite(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<Job> jobs, uniform StructuredBuffer<Entry> entries,
               uniform StructuredBuffer<uint> atlas, uniform RWStructuredBuffer<uint> out,
               uniform RWStructuredBuffer<float4> below) {
    Job j = jobs[id.y];
    uint p = id.x, x = p & ((1u << k.log2) - 1), y = p >> k.log2;
    bool grey = k.channels == 2;  // R, G and B alike: one channel computed for three
    float4 acc = 0;               // premultiplied
    uint depth = 0;               // groups open
    if (x >= (j.x & 0xffff) && x < (j.x >> 16) && y >= (j.y & 0xffff) && y < (j.y >> 16)) {
        for (uint e = j.first; e < j.first + j.count; e++) {
            Entry l = entries[e];
            uint op = l.op & ~kPass;
            float4 src = 0, b = acc;  // a layer's pixel or an isolated group's, to blend over b
            if (op == kLayer) {
                src = l.slot == kSolid ? l.solid : load(atlas, l.slot, p);
                if (l.cover != kNone) src = stroke(src, l.cover == kSolid ? l.cover_solid : coverage(atlas, l.cover, p));
            } else if (op == kPush) {
                open[depth++] = acc;
                if (l.op == kPush) acc = 0;  // isolated: onto transparency, else onto a copy of the backdrop
                continue;
            } else if (op == kPop) {
                b = open[--depth];
                if (l.op != kPop) {  // Pass Through: mixed with the backdrop as it was
                    acc = float4(mix_group(b.r, acc.r, l.alpha), mix_group(b.g, acc.g, l.alpha),
                                 mix_group(b.b, acc.b, l.alpha), mix_group(b.a, acc.a, l.alpha));
                    continue;
                }
                float r = unpremultiply(acc.r, acc.a);  // the group as a layer
                src = grey ? float4(r, r, r, acc.a)
                           : float4(r, unpremultiply(acc.g, acc.a), unpremultiply(acc.b, acc.a), acc.a);
            } else if (op == kSave) {
                below[(l.slot << (2 * k.log2)) + p] = l.level == depth ? acc : open[l.level];
                continue;
            } else {  // kLoad, bottom first
                if (l.level > 0) open[depth++] = acc;
                acc = below[(l.slot << (2 * k.log2)) + p];
                continue;
            }
            acc = over(b, src, l.blend, l.alpha, grey);
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
