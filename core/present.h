#pragma once
// The canvas's frames (plan.md, Ghép; docs/p0-tasks.md, V2a): each device pixel of the canvas takes the document point
// under it (View), samples the composite tiles of the view's mip level there (nearest from 100 % up, bilinear below,
// across tiles), lies over a checkerboard where they are transparent and is the pasteboard's colour past the canvas, in
// the screen's colours: the document's curve to linear light (Source, color.h), a matrix to the screen's linear RGB
// and its curve (Display), RGBA8. On the GPU present.slang reads GpuComposite's tiles into a buffer the canvas copies
// into the texture its window imports; on the CPU the same formulas read composite_tile()'s (offscreen frames, CI).
//
// A frame's tiles whose mips the cache lacks (mip_missing()) show from the finest coarser level it has, while a
// Visible job makes them and then calls on_ready() for another frame. Where no level has them (a level never shown, a
// layer just shown, the tiles a stroke changed) the frame averages up to kFrameMips mip tiles itself, else shows the
// checkerboard there until the job is done. ponytail: hidden layers' mips wait for them to show; a Background job if
// showing one at a level never shown stalls.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "color.h"
#include "gpu.h"
#include "render.h"

namespace pb {

// What a frame shows besides the document, in the screen's 8-bit values.
struct Look {
  std::shared_ptr<const Display> screen;  // its colours: display_profile(screen_profile(...)); null for sRGB's
  std::array<uint8_t, 3> light{255, 255, 255}, dark{204, 204, 204};  // the checkerboard's squares, as Photoshop's
  int cell = 8;  // their side, device pixels, from where the document's top left is on screen
  std::array<uint8_t, 3> pasteboard{40, 40, 40};  // past the canvas
};

// Where a frame goes: View::pixels()' width x height pixels, rows packed, a uint32_t each: R in its low byte, then G, B
// and alpha (255: the canvas hides nothing under it, premultiplied or not), as the canvas (V1) takes them.
struct Target {
  gpu::Buffer* buffer = nullptr;  // a GPU presenter's: its device's, width x height x 4 bytes (Canvas copy()es it on)
  uint32_t* pixels = nullptr;     // a CPU presenter's
};

// Mip tiles a frame averages itself where no level has them yet: about 0.1 s on 10 threads (a data tile 0.3-0.5 ms,
// test_mip's D1 grid), the few above a stroke at pen-up in a few ms.
inline constexpr int kFrameMips = 256;

class Presenter {
 public:
  // On device's GPU when there is one (the device outlives the presenter), else on the CPU. on_ready() runs on a pool
  // thread whenever tiles a frame lacked have come, never once the presenter is being destroyed; it must not destroy
  // it.
  explicit Presenter(gpu::Device* device = nullptr, std::function<void()> on_ready = {});
  ~Presenter();
  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;

  struct Frame {
    bool pending = false;     // tiles are on their way: on_ready() follows
    uint64_t submission = 0;  // a GPU frame's (Device::submit()): wait for it before reading the target
    int tiles = 0;            // of the view's level it shows: in view, and those beside them that bilinear reads
    int coarser = 0;          // of them, shown from a coarser level
    int missing = 0;          // of them, shown as the checkerboard: no level had them
    int built = 0;            // mip tiles it averaged itself, about
  };
  // A frame of `in` as `view` has it (View::pixels(): pixel (i, j) shows the document point at its centre), into
  // `target`. GPU: recorded and submitted, in the buffer once the submission is done; CPU: done on return. An empty
  // canvas makes no frame. Throws std::invalid_argument for no document, or a GPU presenter's target without a buffer,
  // a CPU one's without pixels. One thread at a time: the canvas's frame job (Latest, Interactive: V2b, C3), on the
  // device's thread.
  Frame frame(const RenderInput& in, const View& view, const Target& target, const Look& look = {});

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb
