// pb-bench ps-inputs <dir> [<photo.jpg>]: the images tools/ps-golden's scripts open in Photoshop, made by
// tests/golden.h (golden::inputs, which says what each holds) and written as tests/golden/ps of the repo has them:
// B4's base.png and top.png (tools/ps-golden/blend.jsx composites them in each P0 blend mode; tests/test_ps_golden.cpp
// compares with B1) into <dir>, and P1's into <dir>/p1/in/ (base.png and top.png again, then photo.png, cube.png,
// ramp.png, impulse.png, shapes.png, alpha.png) for tools/ps-golden/p1/*.jsx. Every sample a level Photoshop reads
// back exactly, 16 bits, no colour chunk (sRGB values: the scripts assign sRGB). photo.png is cut from <photo.jpg>,
// ui/mock/photos/monan.jpg of the directory it runs in unless given: run it from the repo's root,
//   build/default/pb-bench ps-inputs tests/golden/ps
// test_golden_inputs checks the files in the repo against golden::inputs.
#include <cstdio>
#include <exception>
#include <filesystem>

#include "../tests/golden.h"
#include "bench.h"

BENCH(ps_inputs) {
  if (args.empty() || args.size() > 2)
    return void(std::printf("ps_inputs: needs a directory: pb-bench ps-inputs <dir> [<photo.jpg>]\n"));
  const std::filesystem::path dir = args[0], photo = args.size() > 1 ? args[1] : "ui/mock/photos/monan.jpg";
  if (!std::filesystem::exists(photo))
    return void(std::printf("ps_inputs: no %s here: run it from the repo's root, or name the photo\n",
                            photo.string().c_str()));
  try {
    const auto files = pb::golden::inputs::all(photo);
    for (const auto& f : files) {
      if (f.name == "base.png" || f.name == "top.png") pb::golden::write_png(dir / f.name, f.image);  // B4's
      pb::golden::write_png(dir / "p1" / "in" / f.name, f.image);
    }
    std::printf("ps_inputs: %s/base.png, top.png; %s/p1/in/: %zu images\n", dir.string().c_str(),
                dir.string().c_str(), files.size());
  } catch (const std::exception& e) {
    std::printf("ps_inputs: %s\n", e.what());
  }
}
