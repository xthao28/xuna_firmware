#include <cassert>
#include <cstdint>
#include <iostream>
#include "DebouncedButton.h"
#include "AnimationManager.h"
int main() {
  using namespace xuna;
  DebouncedButton b;
  b.begin(false, 0);
  assert(!b.update(true, 1));
  assert(!b.update(false, 10));
  assert(!b.update(true, 15));
  assert(!b.update(true, 44));
  assert(b.update(true, 45));
  assert(!b.update(true, 1000)); // held: no repeat
  assert(!b.update(false, 1001));
  assert(!b.update(false, 1031)); // release: no press event
  assert(!b.update(true, 1040));
  assert(b.update(true, 1070));
  b.begin(true, 0); assert(!b.update(true, 50)); // held at boot
  b.begin(false, UINT32_MAX - 20);
  assert(!b.update(true, UINT32_MAX - 10));
  assert(b.update(true, 19)); // rollover
  AnimationManager a;
  a.begin(100); assert(a.update(100)); assert(a.frame() == 0);
  assert(!a.update(499)); assert(a.update(500)); assert(a.frame() == 1);
  a.setState(PetState::Happy, 600);
  assert(a.update(600)); assert(a.frame() == 0);
  assert(!a.update(759)); assert(a.update(760)); assert(a.frame() == 1);
  assert(a.update(2999)); assert(a.state() == PetState::Happy);
  assert(a.update(3000)); assert(a.state() == PetState::Idle); assert(a.frame() == 0);
  a.setState(PetState::Happy, 4000); a.update(4000);
  a.setState(PetState::Happy, 5000); a.update(5000);
  a.update(6400); assert(a.state() == PetState::Happy); // retrigger timer
  a.setState(PetState::Idle, 6500); assert(a.update(6500));
  a.begin(0); a.update(0); assert(a.update(1000000400)); assert(a.frame() == 1);
  a.setState(PetState::Happy, UINT32_MAX - 1000); a.update(UINT32_MAX - 1000);
  assert(a.update(1399)); assert(a.state() == PetState::Idle);
  std::cout << "PASS: debounce, hold/release, boot hold, rollover, frame timing, timeout, retrigger, idle, bounded catch-up\n";
}
