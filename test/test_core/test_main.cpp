#include <unity.h>
#include <cstdint>
#include <xuna/DebouncedButton.h>
#include <xuna/AnimationManager.h>
void test_core_baseline() {
  using namespace xuna;
  DebouncedButton b;
  b.begin(false, 0);
  TEST_ASSERT_TRUE(!b.update(true, 1));
  TEST_ASSERT_TRUE(!b.update(false, 10));
  TEST_ASSERT_TRUE(!b.update(true, 15));
  TEST_ASSERT_TRUE(!b.update(true, 44));
  TEST_ASSERT_TRUE(b.update(true, 45));
  TEST_ASSERT_TRUE(!b.update(true, 1000)); // held: no repeat
  TEST_ASSERT_TRUE(!b.update(false, 1001));
  TEST_ASSERT_TRUE(!b.update(false, 1031)); // release: no press event
  TEST_ASSERT_TRUE(!b.update(true, 1040));
  TEST_ASSERT_TRUE(b.update(true, 1070));
  b.begin(true, 0); TEST_ASSERT_TRUE(!b.update(true, 50)); // held at boot
  b.begin(false, UINT32_MAX - 20);
  TEST_ASSERT_TRUE(!b.update(true, UINT32_MAX - 10));
  TEST_ASSERT_TRUE(b.update(true, 19)); // rollover
  AnimationManager a;
  a.begin(100); TEST_ASSERT_TRUE(a.update(100)); TEST_ASSERT_TRUE(a.frame() == 0);
  TEST_ASSERT_TRUE(!a.update(499)); TEST_ASSERT_TRUE(a.update(500)); TEST_ASSERT_TRUE(a.frame() == 1);
  a.setState(PetState::Happy, 600);
  TEST_ASSERT_TRUE(a.update(600)); TEST_ASSERT_TRUE(a.frame() == 0);
  TEST_ASSERT_TRUE(!a.update(759)); TEST_ASSERT_TRUE(a.update(760)); TEST_ASSERT_TRUE(a.frame() == 1);
  TEST_ASSERT_TRUE(a.update(2999)); TEST_ASSERT_TRUE(a.state() == PetState::Happy);
  TEST_ASSERT_TRUE(a.update(3000)); TEST_ASSERT_TRUE(a.state() == PetState::Idle); TEST_ASSERT_TRUE(a.frame() == 0);
  a.setState(PetState::Happy, 4000); a.update(4000);
  a.setState(PetState::Happy, 5000); a.update(5000);
  a.update(6400); TEST_ASSERT_TRUE(a.state() == PetState::Happy); // retrigger timer
  a.setState(PetState::Idle, 6500); TEST_ASSERT_TRUE(a.update(6500));
  a.begin(0); a.update(0); TEST_ASSERT_TRUE(a.update(1000000400)); TEST_ASSERT_TRUE(a.frame() == 1);
  a.setState(PetState::Happy, UINT32_MAX - 1000); a.update(UINT32_MAX - 1000);
  TEST_ASSERT_TRUE(a.update(1399)); TEST_ASSERT_TRUE(a.state() == PetState::Idle);
  a.setState(PetState::Sleep, 10);a.update(10000);TEST_ASSERT_TRUE(a.state()==PetState::Sleep);
  a.setState(PetState::Curious, 10001);a.update(12401);TEST_ASSERT_TRUE(a.state()==PetState::Idle);
}

void setUp() {}
void tearDown() {}
int main() { UNITY_BEGIN(); RUN_TEST(test_core_baseline); return UNITY_END(); }
