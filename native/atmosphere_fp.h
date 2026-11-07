// SPDX-License-Identifier: MIT
#pragma once
#include <cfenv>
#include <intrin.h>
namespace atmosphere {
// Rendering callbacks must not leak rounding/exception state into simulation.
struct FpScope {
    fenv_t saved{};
    unsigned mxcsr=_mm_getcsr();
    bool valid=fegetenv(&saved)==0;
    ~FpScope(){if(valid)fesetenv(&saved);_mm_setcsr(mxcsr);}
};
}
