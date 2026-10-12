#pragma once
#include <cstdint>
// PCG XSH-RR reference algorithm: Copyright 2014 Melissa O'Neill.
// Licensed under Apache-2.0; see lib/xuna_core/licenses/PCG-Apache-2.0.txt.
// Adapted for XUNA: C++11 instance/interface, no globals, zero-bound guard.
// Source/commit and reference vectors are recorded in firmware README.
namespace xuna {
class IRandom {
 public:
  virtual ~IRandom() {}
  virtual uint32_t next()=0;
  uint32_t below(uint32_t bound) {
    if(!bound)return 0; // invalid range sentinel; consumes no randomness
    const uint32_t threshold=uint32_t(0U-bound)%bound;
    for(;;){const uint32_t value=next();if(value>=threshold)return value%bound;}
  }
};
// Deterministic behavioral RNG, never cryptographic credentials/identity entropy.
class Pcg32 : public IRandom {
 public:
  Pcg32(uint64_t seed,uint64_t stream):state_(0),increment_((stream<<1)|1) {
    next();state_+=seed;next();
  }
  uint32_t next() override {
    const uint64_t previous=state_;
    state_=previous*6364136223846793005ULL+increment_;
    const uint32_t mixed=static_cast<uint32_t>(((previous>>18)^previous)>>27);
    const uint32_t rotation=static_cast<uint32_t>(previous>>59);
    return (mixed>>rotation)|(mixed<<((0U-rotation)&31));
  }
 private:
  uint64_t state_,increment_;
};
}
