#pragma once

// Minimal command-line parsing: "--name value" and "--flag".

#include "tefra/core/types.h"

namespace tefra {

class CommandLine {
  public:
    CommandLine(int argc, char** argv) : argc_(argc), argv_(argv) {}

    bool has(const char* name) const;
    const char* get(const char* name, const char* fallback = nullptr) const;
    i64 get_int(const char* name, i64 fallback) const;
    f64 get_float(const char* name, f64 fallback) const;

  private:
    int find(const char* name) const;
    int argc_;
    char** argv_;
};

} // namespace tefra
