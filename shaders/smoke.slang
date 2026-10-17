// The shader build's smoke test (tests/test_smoke.cpp): dst[i] = src[i] * k.mul + k.add, wrapping, for i < k.n.

struct Consts {
    uint n, mul, add;
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

[shader("compute")]
[numthreads(64, 1, 1)]
void smoke(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<uint> src, uniform RWStructuredBuffer<uint> dst) {
    if (id.x >= k.n) return;
    dst[id.x] = src[id.x] * k.mul + k.add;
}
