#pragma once
// check_adjust (D31): an adjustment on the CPU (Adjustment::apply()) against the GPU (shaders/adjust_apply.slang:
// adjust_pixel() of pack() and tables()) to the bit, on lavapipe here, over every colour of the 33³ lattice (i / 32 a
// channel) and two 16-bit ramps (v / 65535: grey, and R, G, B apart), at pixels of either sign (what a dither
// hashes), the tables placed past others in the buffer (a base other than 0). For each type's tests (D33-D39):
//   pb::test::AdjustCheck check_adjust;
//   CHECK(check_adjust("Levels", R"({"gam":2})"));
// One device for all its checks; none (no Vulkan): every check passes, said once.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "adjust.h"
#include "adjust_apply.spv.h"
#include "check.h"

namespace pb::test {

class AdjustCheck {
 public:
  static constexpr int kRow = 512;            // pixels a row
  static constexpr int kX0 = -300, kY0 = -7;  // the first pixel's place
  static constexpr uint32_t kBase = 13;       // where the tables start in the buffer, past entries of junk

  AdjustCheck() : dev_(gpu_device()) {
    if (!dev_) return;
    kernel_ = dev_->kernel("adjust_apply", adjust_apply_spv, adjust_apply_spv_sizeInBytes, 4, sizeof(Consts));
    for (int b = 0; b <= 32; b++)  // the lattice
      for (int g = 0; g <= 32; g++)
        for (int r = 0; r <= 32; r++) px_.push_back({r / 32.f, g / 32.f, b / 32.f, 0});
    for (uint32_t v = 0; v < 65536; v++) {  // the ramps, as samples become floats (composite.cpp's unit())
      const auto u = [](uint32_t s) { return float(double(s & 0xffff) * (1.0 / 65535)); };
      px_.push_back({u(v), u(v), u(v), 0});
      px_.push_back({u(v), u(65535 - v), u(v * 40503u + 12345u), 0});
    }
    for (size_t i = 0; i < px_.size(); i++) px_[i][3] = float(i % 1021) / 1021;  // alpha passes through
    in_ = dev_->buffer(px_.size() * sizeof(Float4));
    out_ = dev_->buffer(px_.size() * sizeof(Float4));
    params_ = dev_->buffer(sizeof(AdjustParams));
    dev_->upload(*in_, px_.data(), px_.size() * sizeof(Float4));
  }
  explicit operator bool() const { return dev_ != nullptr; }  // a device to check on

  // props.adj and props.vals: false (saying why) when they do not read or the GPU differs from the CPU anywhere.
  bool operator()(const std::string& adj, const std::string& vals = "{}", const AdjustContext& context = {}) {
    const AdjustmentPtr a = adjustment(adj, vals, context);
    if (!a) {
      std::printf("check_adjust: %s %s does not read\n", adj.c_str(), vals.c_str());
      return false;
    }
    return (*this)(*a);
  }
  bool operator()(const Adjustment& a) {
    if (!dev_) return true;
    const size_t n = px_.size();
    // The CPU, a row at a time as the kernel lays them out.
    std::vector<Float4> want = px_;
    for (size_t at = 0; at < n; at += kRow)
      a.apply(want[at].data(), int(std::min<size_t>(kRow, n - at)), kX0, kY0 + int(at / kRow));
    // The GPU.
    const std::vector<Float4>& t = a.tables();
    std::vector<Float4> tables(kBase + std::max<size_t>(t.size(), 1), Float4{-1, 2, -3, 4});
    std::copy(t.begin(), t.end(), tables.begin() + kBase);
    const auto tbuf = dev_->buffer(tables.size() * sizeof(Float4), false);
    dev_->upload(*tbuf, tables.data(), tables.size() * sizeof(Float4));
    const AdjustParams p = a.pack(kBase);
    dev_->upload(*params_, p.w.data(), sizeof p.w);
    const Consts k{kX0, kY0, uint32_t(kRow), uint32_t(n)};
    dev_->fill(*out_, 0xdeadbeef);
    dev_->dispatch(*kernel_, {params_.get(), tbuf.get(), in_.get(), out_.get()}, &k, sizeof k,
                   uint32_t((n + 63) / 64), 1);
    std::vector<Float4> got(n);
    dev_->download(*out_, got.data(), n * sizeof(Float4));
    dev_->flush();
    size_t bad = 0, first = 0;
    for (size_t i = 0; i < n; i++)
      if (std::memcmp(&got[i], &want[i], sizeof(Float4)) != 0 && bad++ == 0) first = i;
    if (bad) {
      const Float4 &c = px_[first], &g = got[first], &w = want[first];
      std::printf("check_adjust: %s %s: %zu of %zu pixels differ, first %zu at (%d, %d): in %.9g %.9g %.9g, "
                  "GPU %.9g %.9g %.9g %.9g, CPU %.9g %.9g %.9g %.9g\n",
                  kAdjNames[size_t(a.type())], a.vals().c_str(), bad, n, first, kX0 + int(first % kRow),
                  kY0 + int(first / kRow), c[0], c[1], c[2], g[0], g[1], g[2], g[3], w[0], w[1], w[2], w[3]);
    }
    return bad == 0;
  }
  size_t pixels() const { return px_.size(); }

 private:
  struct Consts {  // adjust_apply.slang's
    int32_t x0, y0;
    uint32_t width, count;
  };
  std::unique_ptr<gpu::Device> dev_;
  gpu::KernelPtr kernel_;
  gpu::BufferPtr in_, out_, params_;
  std::vector<Float4> px_;
};

}  // namespace pb::test
