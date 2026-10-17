// What a buffer of the GPU composite held, into the larger one that replaces it as it grows (core/gpu_composite.cpp):
// 16 bytes a thread, rows of `row` of them.
struct Consts {
    uint n;    // 16-byte words to copy
    uint row;  // threads in a row of workgroups
};
[[vk::push_constant]] ConstantBuffer<Consts> k;

[shader("compute")]
[numthreads(64, 1, 1)]
void composite_copy(uint3 id: SV_DispatchThreadID, uniform StructuredBuffer<uint4> from,
                    uniform RWStructuredBuffer<uint4> to) {
    uint i = id.y * k.row + id.x;
    if (i < k.n) to[i] = from[i];
}
