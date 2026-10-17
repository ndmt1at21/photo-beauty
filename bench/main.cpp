// Runs the BENCH()es of bench/*.cpp (bench/bench.h): all with no arguments, or one with its own.
#include <algorithm>
#include <cstring>

#include "bench.h"

int main(int argc, char** argv) {
  auto& benches = pb::bench::benches();
  std::sort(benches.begin(), benches.end(), [](const auto& a, const auto& b) { return std::strcmp(a.name, b.name) < 0; });
  if (argc == 1) {
    for (const auto& b : benches) b.run({});
    return 0;
  }
  std::string name = argv[1];
  std::replace(name.begin(), name.end(), '-', '_');
  for (const auto& b : benches)
    if (name == b.name) {
      b.run(pb::bench::Args(argv + 2, argv + argc));
      return 0;
    }
  std::fprintf(stderr, "no benchmark named %s; there are:", argv[1]);
  for (const auto& b : benches) std::fprintf(stderr, " %s", b.name);
  std::fputc('\n', stderr);
  return 1;
}
