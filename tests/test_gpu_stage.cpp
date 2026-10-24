// The HAL's marks and staged uploads (gpu.h, V2d): marks numbered in order, NaN until their submission is waited for,
// then the GPU's times there in order, a kernel's run between two, the last thousand or so kept; uploads the caller
// fills on other threads; uploads into a buffer one after the other, apart or overlapping, into another between and
// read by a kernel after: each as recorded, in order.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "check.h"
#include "jobs.h"
#include "smoke.spv.h"

using namespace pb;

namespace {

// Piece i's bytes: a hash of where they are.
uint8_t byte(size_t i, size_t at) { return uint8_t((i * 2654435761u + at * 40503u) >> 13); }

}  // namespace

TEST(test_gpu_stage) {
  const auto dev = test::gpu_device();
  if (!dev) return;
  const struct {
    uint32_t n, mul, add;
  } k{uint32_t(1) << 21, 3, 1};
  auto kernel = dev->kernel("smoke", smoke_spv, smoke_spv_sizeInBytes, 2, sizeof k);

  // Marks around a kernel over 2M words.
  {
    auto a = dev->buffer(size_t(k.n) * 4), b = dev->buffer(size_t(k.n) * 4);
    dev->fill(*a, 7);
    const uint64_t m0 = dev->mark(), m1 = dev->mark();
    dev->dispatch(*kernel, {a.get(), b.get()}, &k, sizeof k, k.n / 64, 1);
    const uint64_t m2 = dev->mark();
    CHECK(m0 < m1 && m1 < m2);
    const uint64_t s = dev->submit();
    CHECK(std::isnan(dev->marked(m0)) && std::isnan(dev->marked(m2)));  // not waited for yet
    dev->wait(s);
    const double t0 = dev->marked(m0), t1 = dev->marked(m1), t2 = dev->marked(m2);
    std::printf("gpu_stage: %s: a kernel over %u words in %.3f ms\n", dev->name().c_str(), k.n, t2 - t1);
    CHECK(t0 <= t1 && t1 < t2);  // NaN fails: lavapipe and the GPUs P0 runs on keep time on a compute queue
    CHECK(std::isnan(dev->marked(m2 + 1)));  // none made yet
    // The 64th of a submission has its time, the 65th none; 20 submissions on, the first ones are gone.
    std::vector<uint64_t> ms;
    for (int j = 0; j < 20; j++) {
      for (int i = 0; i < 65; i++) ms.push_back(dev->mark());
      dev->flush();
    }
    CHECK(!std::isnan(dev->marked(ms[ms.size() - 2])) && std::isnan(dev->marked(ms.back())));
    CHECK(dev->marked(ms[ms.size() - 2]) >= dev->marked(ms[ms.size() - 65]));
    CHECK(std::isnan(dev->marked(m2)) && std::isnan(dev->marked(ms[0])));
  }

  // stage(): 64 pieces of 256 KB into a buffer out of order, filled on the pool's threads.
  {
    constexpr size_t piece = size_t(256) << 10, pieces = 64;
    auto c = dev->buffer(piece * pieces);
    std::vector<uint8_t*> to;
    for (size_t i = 0; i < pieces; i++) to.push_back(static_cast<uint8_t*>(dev->stage(*c, piece, (i * 37 % pieces) * piece)));
    parallel_for(int(pieces), [&](int begin, int end) {
      for (int i = begin; i < end; i++)
        for (size_t at = 0; at < piece; at++) to[size_t(i)][at] = byte(size_t(i), at);
    });
    std::vector<uint8_t> got(piece * pieces);
    dev->download(*c, got.data(), got.size());
    dev->flush();
    for (size_t i = 0; i < pieces; i++)
      for (size_t at = 0; at < piece; at++) CHECK(got[(i * 37 % pieces) * piece + at] == byte(i, at));
  }

  // upload()s into x apart and overlapping, into y between, then the kernel reading x, then x again.
  {
    constexpr uint32_t n = 4096;  // words of x and y
    const struct {
      uint32_t n, mul, add;
    } kx{n, 5, 2};
    auto x = dev->buffer(n * 4), y = dev->buffer(n * 4);
    std::vector<uint32_t> want(n, 0), v(n);
    dev->fill(*x, 0);
    const auto put = [&](gpu::Buffer& to, uint32_t from, uint32_t words, uint32_t value, std::vector<uint32_t>* mirror) {
      std::fill(v.begin(), v.begin() + words, value);
      dev->upload(to, v.data(), words * 4, from * 4);
      if (mirror) std::fill(mirror->begin() + from, mirror->begin() + from + words, value);
    };
    put(*x, 0, 1024, 1, &want);
    put(*x, 2048, 1024, 2, &want);
    put(*x, 512, 1024, 3, &want);  // over the first: after it
    put(*y, 0, n, 4, nullptr);
    put(*x, 3072, 1024, 5, &want);
    put(*x, 3000, 10, 6, &want);  // over the second and the fourth
    dev->dispatch(*kernel, {x.get(), y.get()}, &kx, sizeof kx, n / 64, 1);
    std::vector<uint32_t> wy(n);
    for (uint32_t i = 0; i < n; i++) wy[i] = want[i] * kx.mul + kx.add;
    put(*x, 0, 4, 7, &want);  // after the kernel read it
    std::vector<uint32_t> gx(n), gy(n);
    dev->download(*x, gx.data(), n * 4);
    dev->download(*y, gy.data(), n * 4);
    dev->flush();
    CHECK(gx == want && gy == wy);
  }
}
