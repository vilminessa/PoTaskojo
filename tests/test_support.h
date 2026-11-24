#pragma once
// Minimal test harness: one binary, no third-party dependency, CTest-friendly.
//
// Cases register themselves at static-init time; main() runs them all and
// returns non-zero when anything failed, which is what ctest expects.

#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

namespace testing {

struct Case {
  const char* name;
  void (*fn)();
};

inline std::vector<Case>& registry() {
  static std::vector<Case> r;
  return r;
}

inline int& failure_count() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void report_failure(const char* file, int line, const std::string& what) {
  ++failure_count();
  std::cout << "    FAIL " << file << ":" << line << "  " << what << "\n";
}

// Pretty-printers. Kept intentionally small: they only have to make a failed
// CHECK_EQ readable.
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) {
  return "\"" + std::string(v ? v : "(null)") + "\"";
}
inline std::string show(const std::wstring& v) {
  std::string s;
  s.reserve(v.size());
  for (wchar_t c : v)
    s += (c >= 32 && c < 127) ? static_cast<char>(c) : '?';
  return "\"" + s + "\"";
}
template <class T>
  requires std::is_arithmetic_v<T>
std::string show(T v) {
  return std::to_string(v);
}

inline int run_all() {
  int failed_cases = 0;
  for (const auto& c : registry()) {
    const int before = failure_count();
    std::cout << "[ RUN      ] " << c.name << "\n";
    c.fn();
    if (failure_count() != before) {
      ++failed_cases;
      std::cout << "[   FAIL   ] " << c.name << "\n";
    } else {
      std::cout << "[       OK ] " << c.name << "\n";
    }
  }
  std::cout << "\n"
            << (registry().size() - failed_cases) << " passed, " << failed_cases
            << " failed, " << registry().size() << " total\n";
  return failed_cases == 0 ? 0 : 1;
}

}  // namespace testing

// Defines and registers a test case. Names must be unique inside the binary.
#define TEST_CASE(name)                                     \
  static void name();                                       \
  static ::testing::Registrar reg_##name(#name, &name);     \
  static void name()

#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond))                                                        \
      ::testing::report_failure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
  } while (0)

// Types must match: a signed/unsigned mismatch is a warning, and warnings are
// errors in CI. Values are taken by value so an expression like
// `store().all()[0]` cannot leave a dangling reference behind.
#define CHECK_EQ(a, b)                                                     \
  do {                                                                     \
    const auto va = (a);                                                   \
    const auto vb = (b);                                                   \
    if (!(va == vb))                                                       \
      ::testing::report_failure(__FILE__, __LINE__,                        \
                                std::string(#a " == " #b "  (") +          \
                                    ::testing::show(va) + " vs " +         \
                                    ::testing::show(vb) + ")");            \
  } while (0)
