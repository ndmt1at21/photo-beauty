// pb-bench ps-strokes <dir>: what tools/ps-golden/strokes.jsx (C41) paints on in Photoshop and the paths it strokes,
// made by tests/ps_strokes_inputs.h (golden::strokes_in, which says what each holds) and written as
// tests/golden/ps/strokes/in/ of the repo has them: <dir>/strokes/in/white.png, patches.png, tones.png, skin.png,
// paths.json. Run it from the repo's root,
//   build/default/pb-bench ps-strokes tests/golden/ps
// test_ps_strokes_inputs checks the files in the repo against golden::strokes_in.
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../tests/ps_strokes_inputs.h"
#include "bench.h"

BENCH(ps_strokes) {
  if (args.size() != 1) return void(std::printf("ps_strokes: needs a directory: pb-bench ps-strokes <dir>\n"));
  namespace in = pb::golden::strokes_in;
  const std::filesystem::path dir = in::dir(args[0]);
  try {
    const auto files = in::all();
    for (const auto& f : files) pb::golden::write_png(dir / f.name, f.image);
    std::ofstream json(dir / "paths.json", std::ios::binary);
    json << in::paths_json();
    if (!json.flush()) throw std::runtime_error("cannot write " + (dir / "paths.json").string());
    std::printf("ps_strokes: %s/: %zu images, paths.json (%zu paths)\n", dir.generic_string().c_str(), files.size(),
                in::paths().size());
  } catch (const std::exception& e) {
    std::printf("ps_strokes: %s\n", e.what());
  }
}
