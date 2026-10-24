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
//
// A view change composites the tiles it newly shows within a budget (layer tiles into the GPU's atlas, as many as the
// frame's time leaves room for: UploadBudget; on the CPU kFrameUploads that composite_tile() reads), from the
// pointer's tile out as it zooms or turns (the centre's as it pans); the others show meanwhile from the level below
// where it has them composited (the level a zoom out comes from), else the finest above that has, the top's (the
// canvas in one tile) at last, and the next frames go on (on_ready(), once a job brought their layer tiles back into
// RAM). A GPU frame with budget left and nothing waiting reads ahead of the motion, the tiles whose layer tiles a job
// brought into RAM: a zoom in the next level's tiles about the pointer, as the view will be once it gets there; else
// the view the motion reaches some frames on. A view at rest has the job bring in those of a zoom in about its centre
// (for a zoom to start from), and the GPU's cache of composite tiles grow to hold every level's (an allocation and a
// copy at rest rather than in a zoom's frame). What changed under the view (an edit, the stroke) always composites in
// the frame.
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

// Layer tiles a CPU frame reads for the tiles a view change newly shows (the first such tile even past it).
inline constexpr int kFrameUploads = 20;

// The layer tiles a GPU frame may upload for the tiles a view change newly shows and to read ahead (the first tile
// even past it): as many as fit in kFrameMs beside the rest of the frame, at the cost of an upload lately. A frame
// lasts, as its waiter sees it, from its start to its first submission (the GPU starts) and then on the GPU until done
// (gpu.h's marks; the CPU stages reading ahead meanwhile); an upload costs its copy and its share of the composite on
// the GPU, and staging where the GPU waits for it. On the Iris Xe (V2d, D1) some 0.1 ms each, the GPU's beside 3 to 5
// ms of the rest (present.slang, plans). A frame's times come in once its submission was waited for. ponytail: one
// target for every machine and the app's frame (which adds the canvas's copy and Qt's render); a target from the
// display's refresh if that shows.
class UploadBudget {
 public:
  static constexpr double kFrameMs = 12;
  static constexpr int kMin = 8, kMax = 400;
  int next() const;
  // A frame's ms but its uploads', its uploads' and how many.
  void took(double other_ms, double upload_ms, int uploads);

 private:
  double other_ = 4, each_ = 0.3;  // moving averages
};

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
    int finer = 0;            // of them, shown from the level below, composited already, while theirs come
    int missing = 0;          // of them, shown as the checkerboard: no level had them
    int built = 0;            // mip tiles it averaged itself, about
  };
  // A frame of `in` as `view` has it (View::pixels(): pixel (i, j) shows the document point at its centre), into
  // `target`. GPU: recorded and submitted, in the buffer once the submission is done; CPU: done on return. An empty
  // canvas makes no frame. Throws std::invalid_argument for no document, or a GPU presenter's target without a buffer,
  // a CPU one's without pixels. One thread at a time: the canvas's frame job (Latest, Interactive: V2b, C3), on the
  // device's thread.
  Frame frame(const RenderInput& in, const View& view, const Target& target, const Look& look = {});

  // The layer tiles a frame may spend on the tiles a view change newly shows and on reading ahead, from now on: the
  // frame's time decides (a CPU one's kFrameUploads) until set. For tests and benches; the frame's thread.
  void set_upload_budget(int tiles);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace pb
