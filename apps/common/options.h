#pragma once

#include "runner.h"

#include "tefra/core/cli.h"

namespace tefra::app {

// Parses the options shared by all apps into cfg. Returns false (after logging) on invalid input.
bool parse_common_options(const CommandLine& cl, RunConfig& cfg);
void print_common_usage();

} // namespace tefra::app
