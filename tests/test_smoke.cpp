// The build's plumbing: snapone's pool, a Slang kernel of shaders/ on a Vulkan device, the version.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "check.h"
#include "jobs.h"
#include "smoke.spv.h"
#include "version.h"

TEST(test_smoke) {
  CHECK(std::strlen(pb::version()) >= 5);  // "0.1.0"

  // smoke.slang's formula on the CPU over parallel_for, each index once; n is not a multiple of the group size.
  constexpr uint32_t n = 100'003, mul = 2'654'435'761u, add = 12'345;
  std::vector<uint32_t> src(n), want(n);
  for (uint32_t i = 0; i < n; i++) src[i] = i ^ (i << 9);
  std::atomic<uint32_t> done{0};
  snapone::parallel_for(int(n), [&](int begin, int end) {
    for (int i = begin; i < end; i++) want[i] = src[i] * mul + add;
    done += uint32_t(end - begin);
  });
  CHECK(done == n);  // with every index right below: each once
  for (uint32_t i = 0; i < n; i++) CHECK(want[i] == src[i] * mul + add);

  auto dev = pb::test::gpu_device();
  if (!dev) return;
  std::printf("test_smoke: %s, %s\n", dev->name().c_str(), dev->driver().c_str());
  const struct {
    uint32_t n, mul, add;
  } k{n, mul, add};
  auto kernel = dev->kernel("smoke", smoke_spv, smoke_spv_sizeInBytes, 2, sizeof k);
  auto in = dev->buffer(n * 4), out = dev->buffer(n * 4);
  dev->upload(*in, src.data(), n * 4);
  dev->fill(*out, 0);
  dev->dispatch(*kernel, {in.get(), out.get()}, &k, sizeof k, (n + 63) / 64, 1);
  std::vector<uint32_t> got(n);
  dev->download(*out, got.data(), n * 4);
  dev->flush();
  CHECK(got == want);
}
