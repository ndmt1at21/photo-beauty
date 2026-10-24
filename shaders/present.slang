// The canvas's frames (core/present.cpp, whose shade() is this line by line on the CPU): a thread a device pixel takes
// the document point under it, the tile of the frame's mip level there and the pass that shows it (`choice`: one per
// level, coarser ones where the frame's lacks mips, none where no level has them), samples this pass's level (nearest
// from 100 % up, bilinear below on premultiplied texels, across tiles: one this pass lacks lends the texel nearest in
// the pixel's own), and lays it over the checkerboard in the screen's colours: the document's curve to linear, one
// matrix to the screen's linear RGB, its curve, RGBA8. The first pass also writes the pasteboard past the canvas and
// the checkerboard where no level has the tile. Every float operation `precise` (no fused multiply-add), as the CPU
// rounds, so that the two place and sample pixels alike.
import blend;

struct Consts {
    float ox, oy, axx, axy, ayx, ayy;  // pixel (i, j)'s centre -> document point o + i ax + j ay (View::pixels()),
                                       // from the first pass's window's corner
    float lox, loy, hix, hiy;          // the canvas likewise: lo <= p < hi
    float base;                        // texels of the frame's level a document pixel: its tiles pick the pass
    float scale, offx, offy;           // this pass's texels from its window's corner: p scale + off
    int minx, miny, maxx, maxy;        // the canvas at this pass's level, texels from there
    uint log2;                         // kTileLog2: tiles are 2^log2 texels across
    uint cw, ch;                       // the choice table's tiles across, down: the first pass's window, at tables[0]
    uint tw, th, table;                // this pass's slots: tiles across, down of its window, at tables[table]
    uint pass, flags;                  // this pass in the choice table; kBilinear | kIdentity
    float inv_cell, phasex, phasey;    // the checkerboard: 1 / its squares' side; where pixel indices meet the
                                       // document's corner, mod 2 sides
    uint width, height;                // the frame, device pixels
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

static const uint kBilinear = 1, kIdentity = 2;  // flags: below 100 %; colours as they are (sRGB on sRGB)
static const uint kNone = 0xffffffff, kEmpty = 0xfffffffe;  // a slot: not in this pass; transparent throughout
static const uint kNoChoice = 0xffffffff;                   // a choice: no level has the tile
// The colour table (core/present.cpp): the matrix's rows from 0, the checkerboard's squares at 9 and 12, the
// pasteboard at 15 (R, G, B 0..1); the document's curves (kLut each) from kLin, the screen's (kCurve each) from kOut.
static const uint kLut = 4096, kCurve = 1024, kLin = 32, kOut = 32 + 3 * 4096;

// The slot of this pass's tile holding texel (x, y) (from its window's corner): kNone outside the window.
uint slot(StructuredBuffer<uint> tables, int x, int y) {
    int tx = x >> k.log2, ty = y >> k.log2;
    if (tx < 0 || ty < 0 || tx >= int(k.tw) || ty >= int(k.th)) return kNone;
    return tables[k.table + uint(ty) * k.tw + uint(tx)];
}

// Texel (x, y) of the tile in slot e as R, G, B, alpha, not premultiplied: GpuComposite's RGBA U16
// (core/gpu_composite.h).
float4 read(StructuredBuffer<uint> tiles, uint e, int x, int y) {
    if (e >= kEmpty) return float4(0);
    uint mask = (1u << k.log2) - 1;
    uint i = ((e << (2 * k.log2)) + ((uint(y) & mask) << k.log2) + (uint(x) & mask)) * 2;
    uint w0 = tiles[i], w1 = tiles[i + 1];
    return float4(unit16(w0 & 0xffff), unit16(w0 >> 16), unit16(w1 & 0xffff), unit16(w1 >> 16));
}

// Texel (x, y); in a tile the pass lacks, the nearest of the pixel's own tile (its corner `own`) instead.
float4 texel(StructuredBuffer<uint> tiles, StructuredBuffer<uint> tables, int x, int y, int2 own) {
    uint e = slot(tables, x, y);
    if (e == kNone) {
        int last = (1 << k.log2) - 1;
        x = clamp(x, own.x, own.x + last);
        y = clamp(y, own.y, own.y + last);
        e = slot(tables, x, y);
    }
    return read(tiles, e, x, y);
}

float4 premultiplied(float4 t) {
    precise float4 p = float4(t.rgb * t.a, t.a);
    return p;
}

// This pass's level at document point (px, py): R, G, B, alpha, not premultiplied.
float4 sample(StructuredBuffer<uint> tiles, StructuredBuffer<uint> tables, float px, float py) {
    precise float u = px * k.scale + k.offx, v = py * k.scale + k.offy;
    int iu = clamp(int(floor(u)), k.minx, k.maxx), iv = clamp(int(floor(v)), k.miny, k.maxy);
    int2 own = int2((iu >> k.log2) << k.log2, (iv >> k.log2) << k.log2);
    if ((k.flags & kBilinear) == 0) return texel(tiles, tables, iu, iv, own);
    precise float fu = u - 0.5, fv = v - 0.5;
    int x0 = int(floor(fu)), y0 = int(floor(fv));
    precise float wx = fu - float(x0), wy = fv - float(y0);
    int xa = clamp(x0, k.minx, k.maxx), xb = clamp(x0 + 1, k.minx, k.maxx);
    int ya = clamp(y0, k.miny, k.maxy), yb = clamp(y0 + 1, k.miny, k.maxy);
    // The four in one tile this pass has (most pixels): its slot read once, as each texel() would find it.
    uint e = (xa >> k.log2) == (xb >> k.log2) && (ya >> k.log2) == (yb >> k.log2) ? slot(tables, xa, ya) : kNone;
    float4 t00, t10, t01, t11;
    if (e != kNone) {
        t00 = premultiplied(read(tiles, e, xa, ya));
        t10 = premultiplied(read(tiles, e, xb, ya));
        t01 = premultiplied(read(tiles, e, xa, yb));
        t11 = premultiplied(read(tiles, e, xb, yb));
    } else {
        t00 = premultiplied(texel(tiles, tables, xa, ya, own));
        t10 = premultiplied(texel(tiles, tables, xb, ya, own));
        t01 = premultiplied(texel(tiles, tables, xa, yb, own));
        t11 = premultiplied(texel(tiles, tables, xb, yb, own));
    }
    precise float w00 = (1 - wx) * (1 - wy), w10 = wx * (1 - wy), w01 = (1 - wx) * wy, w11 = wx * wy;
    precise float4 p = t00 * w00 + t10 * w10 + t01 * w01 + t11 * w11;
    if (!(p.a > 0)) return float4(0);
    if (p.a == 1) return p;  // what the CPU's division by 1 gives
    precise float4 c = float4(p.rgb / p.a, p.a);
    return c;
}

// Table `at` of n entries at position pos (0 .. n - 1), interpolated.
float lut(StructuredBuffer<float> color, uint at, uint n, float pos) {
    uint i = min(uint(pos), n - 2);
    precise float f = pos - float(i);
    precise float v = color[at + i] + f * (color[at + i + 1] - color[at + i]);
    return v;
}

// The document's encoded R, G, B as the screen's: linear, the matrix (clipped to the screen's gamut), its curve.
float3 shown(StructuredBuffer<float> color, float3 e) {
    precise float3 l = float3(lut(color, kLin, kLut, saturate(e.r) * float(kLut - 1)),
                              lut(color, kLin + kLut, kLut, saturate(e.g) * float(kLut - 1)),
                              lut(color, kLin + 2 * kLut, kLut, saturate(e.b) * float(kLut - 1)));
    precise float3 d = saturate(float3(color[0] * l.r + color[1] * l.g + color[2] * l.b,
                                       color[3] * l.r + color[4] * l.g + color[5] * l.b,
                                       color[6] * l.r + color[7] * l.g + color[8] * l.b));
    return float3(lut(color, kOut, kCurve, sqrt(d.r) * float(kCurve - 1)),
                  lut(color, kOut + kCurve, kCurve, sqrt(d.g) * float(kCurve - 1)),
                  lut(color, kOut + 2 * kCurve, kCurve, sqrt(d.b) * float(kCurve - 1)));
}

// The checkerboard's square at pixel (i, j): light where the squares across and down add up even.
float3 checker(StructuredBuffer<float> color, float i, float j) {
    precise float a = (i - k.phasex) * k.inv_cell, b = (j - k.phasey) * k.inv_cell;
    uint at = ((int(floor(a)) + int(floor(b))) & 1) != 0 ? 12 : 9;
    return float3(color[at], color[at + 1], color[at + 2]);
}

uint pack(float3 v) {
    precise float3 x = saturate(v) * 255.0 + 0.5;
    uint3 o = uint3(x);
    return o.r | (o.g << 8) | (o.b << 16) | 0xff000000u;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void present(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<uint> tiles, uniform StructuredBuffer<uint> tables,
             uniform StructuredBuffer<float> color, uniform RWStructuredBuffer<uint> out) {
    if (id.x >= k.width || id.y >= k.height) return;
    float s = float(id.x), t = float(id.y);
    precise float px = k.ox + s * k.axx + t * k.ayx, py = k.oy + s * k.axy + t * k.ayy;
    uint i = id.y * k.width + id.x;
    bool first = k.pass == 0;
    if (!(px >= k.lox && px < k.hix && py >= k.loy && py < k.hiy)) {  // past the canvas
        if (first) out[i] = pack(float3(color[15], color[16], color[17]));
        return;
    }
    precise float qx = px * k.base, qy = py * k.base;
    int cx = int(floor(qx)) >> k.log2, cy = int(floor(qy)) >> k.log2;
    uint choice = cx >= 0 && cy >= 0 && cx < int(k.cw) && cy < int(k.ch) ? tables[uint(cy) * k.cw + uint(cx)] : kNoChoice;
    if (choice != k.pass && !(first && choice == kNoChoice)) return;
    float3 bg = checker(color, s, t);
    if (choice == kNoChoice) {
        out[i] = pack(bg);
        return;
    }
    float4 c = sample(tiles, tables, px, py);
    float3 e = (k.flags & kIdentity) != 0 ? c.rgb : shown(color, c.rgb);
    precise float3 o = e * c.a + bg * (1 - c.a);
    out[i] = pack(o);
}
