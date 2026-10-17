#pragma once
// What photo-beauty's self-checks share. CHECK instead of assert so they also run in release builds (ctest).
// TEST(test_x) { ... } in any tests/*.cpp defines a test and registers it: `pb-tests` runs them all in name order,
// `pb-tests test_x test_y` those (tests/main.cpp).
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#include "gpu.h"

#define CHECK(x)                                                              \
  do {                                                                        \
    if (!(x)) {                                                               \
      std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #x); \
      std::exit(1);                                                           \
    }                                                                         \
  } while (0)

namespace pb::test {

struct Test {
  const char* name;
  void (*run)();
};
inline std::vector<Test>& tests() {
  static std::vector<Test> all;
  return all;
}
struct Add {
  Add(const char* name, void (*run)()) { tests().push_back({name, run}); }
};

// A Vulkan device, lavapipe where there is no GPU; null, after saying so, where there is none at all (CI's
// Windows and macOS runners): GPU checks return early there.
inline std::unique_ptr<gpu::Device> gpu_device() {
  auto dev = gpu::Device::create(true);
  if (!dev) std::printf("skipping GPU checks: no Vulkan device\n");
  return dev;
}

}  // namespace pb::test

#define TEST(name)                                    \
  static void name();                                 \
  static const ::pb::test::Add name##_add{#name, name}; \
  static void name()
