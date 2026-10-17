#pragma once
// What pb-bench's benchmarks share. BENCH(name) { ... } in any bench/*.cpp defines one and registers it:
// `pb-bench` runs them all in name order with no arguments (one that needs some says so and returns),
// `pb-bench <name> [arguments]` one with its own, a dash for an underscore (`pb-bench ps-inputs` runs
// BENCH(ps_inputs)). Each number prints as one line `name: value unit` (report()), for CI's step summary.
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace pb::bench {

using Args = std::vector<std::string>;
struct Bench {
  const char* name;
  void (*run)(const Args&);
};
inline std::vector<Bench>& benches() {
  static std::vector<Bench> all;
  return all;
}
struct Add {
  Add(const char* name, void (*run)(const Args&)) { benches().push_back({name, run}); }
};

using Clock = std::chrono::steady_clock;
inline double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

inline void report(const char* name, double value, const char* unit) {
  std::printf("%s: %.6g %s\n", name, value, unit);
  std::fflush(stdout);
}

}  // namespace pb::bench

#define BENCH(name)                                                          \
  static void bench_##name(const ::pb::bench::Args&);                        \
  static const ::pb::bench::Add bench_##name##_add{#name, bench_##name};     \
  static void bench_##name([[maybe_unused]] const ::pb::bench::Args& args)
