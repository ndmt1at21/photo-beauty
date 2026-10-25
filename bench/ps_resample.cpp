// pb-bench ps-resample <dir> [<photo.jpg>]: the inputs of tools/ps-golden/resample.jsx (T38) into <dir>, the repo's
// tests/golden/ps/resample/in: checker.png, ramp.png, photo.png and their padded copies, made by
// tests/ps_resample_inputs.h (golden::resample, which says what each holds). photo.png is cut from B30's photo.png,
// made of <photo.jpg> (ui/mock/photos/monan.jpg of the directory it runs in unless given): run it from the repo's root,
//   build/default/pb-bench ps-resample tests/golden/ps/resample/in
// test_ps_resample_inputs checks the files in the repo against golden::resample.
#include <cstdio>
#include <exception>
#include <filesystem>

#include "../tests/ps_resample_inputs.h"
#include "bench.h"

BENCH(ps_resample) {
  if (args.empty() || args.size() > 2)
    return void(std::printf("ps_resample: needs a directory: pb-bench ps-resample <dir> [<photo.jpg>]\n"));
  const std::filesystem::path dir = args[0], photo = args.size() > 1 ? args[1] : "ui/mock/photos/monan.jpg";
  if (!std::filesystem::exists(photo))
    return void(std::printf("ps_resample: no %s here: run it from the repo's root, or name the photo\n",
                            photo.string().c_str()));
  try {
    const auto files = pb::golden::resample::all(photo);
    for (const auto& f : files) pb::golden::write_png(dir / f.name, f.image);
    std::printf("ps_resample: %s/: %zu images\n", dir.string().c_str(), files.size());
  } catch (const std::exception& e) {
    std::printf("ps_resample: %s\n", e.what());
  }
}
