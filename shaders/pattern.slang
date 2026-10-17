// The canvas's test card (V1; Canvas with PB_CANVAS=pattern): what pattern_pixel() of core/view.cpp computes, to the
// bit, a thread a device pixel of the frame. Every float operation is `precise` (no fused multiply-add) and in the
// CPU's order, so both find the same document pixel under each device pixel; the rest is integer.

struct Consts {
    float2 origin, across, down;  // the document point at the centre of device pixel (i, j): origin + i across + j down
    int grid;                     // grid lines every 2^grid document pixels
    int width, height;            // the frame, device pixels
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

// The document pixel under the centre of device pixel (i, j).
int2 under(int i, int j) {
    precise float fi = float(i), fj = float(j);
    precise float u = k.origin.x + fi * k.across.x + fj * k.down.x;
    precise float v = k.origin.y + fi * k.across.y + fj * k.down.y;
    return int2(clamp(floor(float2(u, v)), -1073741824.0, 1073741824.0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void pattern(uint3 id: SV_DispatchThreadID, uniform RWStructuredBuffer<uint> out) {
    int i = int(id.x), j = int(id.y);
    if (i >= k.width || j >= k.height) return;
    int2 p = under(i, j), cell = p >> k.grid;
    bool line = any(cell != (under(i - 1, j) >> k.grid)) || any(cell != (under(i, j - 1) >> k.grid));
    uint r = uint(p.x >> 4) & 255, g = uint(p.y >> 4) & 255, b = (((cell.x >> 3) + (cell.y >> 3)) & 1) != 0 ? 224 : 96;
    if (line) {
        r = 255 - r;
        g = 255 - g;
        b = 255 - b;
    }
    out[j * k.width + i] = r | (g << 8) | (b << 16) | 0xff000000;
}
