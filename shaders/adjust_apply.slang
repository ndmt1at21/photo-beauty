// What tests/adjust_check.h compares with the CPU: adjust_pixel() of one adjustment (params[0], its tables in
// `tables`) over `count` pixels of `px`, a thread a pixel, rows `width` pixels wide from (x0, y0) (where a dither
// hashes); alpha passes through.
import adjust;

struct Consts {
    int x0, y0;
    uint width;  // pixels a row
    uint count;  // pixels
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

[shader("compute")]
[numthreads(64, 1, 1)]
void adjust_apply(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<AdjustParams> params,
                  uniform StructuredBuffer<float4> tables, uniform StructuredBuffer<float4> px,
                  uniform RWStructuredBuffer<float4> out) {
    uint p = id.x;
    if (p >= k.count) return;
    int x = k.x0 + int(p % k.width), y = k.y0 + int(p / k.width);
    float4 c = px[p];
    out[p] = float4(adjust_pixel(params[0], tables, c.rgb, x, y), c.a);
}
