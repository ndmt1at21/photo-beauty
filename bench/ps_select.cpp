// pb-bench ps-select <dir>: the selections tools/ps-golden/select.jsx (S39) loads in Photoshop, made by
// tests/ps_select_inputs.h (golden::select_in, which says what each holds) and written as tests/golden/ps/select/in/ of
// the repo has them: <dir>/select/in/hard.png, aa.png, soft.png, ramp-x.png, ramp-y.png. Run it from the repo's root,
//   build/default/pb-bench ps-select tests/golden/ps
// test_ps_select_inputs checks the files in the repo against golden::select_in.
#include <cstdio>
#include <exception>
#include <filesystem>

#include "../tests/ps_select_inputs.h"
#include "bench.h"

BENCH(ps_select) {
  if (args.size() != 1) return void(std::printf("ps_select: needs a directory: pb-bench ps-select <dir>\n"));
  const std::filesystem::path dir = pb::golden::select_in::dir(args[0]);
  try {
    const auto files = pb::golden::select_in::all();
    for (const auto& f : files) pb::golden::write_png(dir / f.name, f.image);
    std::printf("ps_select: %s/: %zu images\n", dir.generic_string().c_str(), files.size());
  } catch (const std::exception& e) {
    std::printf("ps_select: %s\n", e.what());
  }
}
