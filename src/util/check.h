#pragma once

#include <iostream>

static inline void check(bool condition, const char* file, int line,
                         const char* message) {
  if (!condition) {
    std::cerr << file << ':' << line << ": check failed: " << message << '\n';
    std::abort();
  }
}
#define check(expr) check(static_cast<bool>(expr), __FILE__, __LINE__, #expr)
