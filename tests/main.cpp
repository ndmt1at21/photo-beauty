// Runs the TEST()s of tests/*.cpp (tests/check.h): all in name order, or those named on the command line.
#include <algorithm>
#include <cstring>

#include "check.h"

int main(int argc, char** argv) {
  auto& tests = pb::test::tests();
  std::sort(tests.begin(), tests.end(), [](const auto& a, const auto& b) { return std::strcmp(a.name, b.name) < 0; });
  const auto named = [](const pb::test::Test& t, const char* name) { return std::strcmp(t.name, name) == 0; };
  for (int i = 1; i < argc; i++)
    if (std::none_of(tests.begin(), tests.end(), [&](const auto& t) { return named(t, argv[i]); })) {
      std::fprintf(stderr, "no test named %s\n", argv[i]);
      return 1;
    }
  int ran = 0;
  for (const auto& t : tests)
    if (argc == 1 || std::any_of(argv + 1, argv + argc, [&](const char* a) { return named(t, a); })) t.run(), ran++;
  if (ran == 0) {  // registration broke: nothing would pass silently
    std::fputs("no tests\n", stderr);
    return 1;
  }
  std::printf("tests passed: %d\n", ran);
}
