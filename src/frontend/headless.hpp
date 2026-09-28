#pragma once

#include "options.hpp"

namespace gbemu {

// Exit codes: 0 pass (or finished a fixed frame count), 1 fail, 2 timeout, 3 error.
int run_headless(const Options& options);

}  // namespace gbemu
