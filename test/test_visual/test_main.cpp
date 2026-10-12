#include <unity.h>
#include <xuna/visual/PetVisualAnimation.h>
using namespace xuna;
struct ZeroRandom:IRandom{uint32_t next() override{return 2001U*2501U;}};
void timing(){ZeroRandom random;PetVisualAnimation animation;animation.begin(0,random);
 TEST_ASSERT_EQUAL_UINT8(0,animation.frame(0,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(10,animation.frame(1800,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(2,animation.frame(3000,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(4,animation.frame(3100,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(6,animation.frame(3200,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(0,animation.frame(3300,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(4,animation.frame(3300,PetState::Sleep,random));
 TEST_ASSERT_EQUAL_UINT8(9,animation.frame(3300,PetState::Happy,random));
 animation.begin(UINT32_MAX-100,random);
 TEST_ASSERT_EQUAL_UINT8(2,animation.frame(2899,PetState::Idle,random));
}
void wiggle_and_late(){ZeroRandom random;PetVisualAnimation animation;animation.begin(0,random);
 const auto a=animation.frame(6500,PetState::Idle,random);TEST_ASSERT_EQUAL_UINT8(1,a%2);
 TEST_ASSERT_EQUAL_UINT8(0,animation.frame(6660,PetState::Idle,random)%2);
 TEST_ASSERT_EQUAL_UINT8(1,animation.frame(6820,PetState::Idle,random)%2);
 TEST_ASSERT_EQUAL_UINT8(0,animation.frame(6820,PetState::Sleep,random)%2);
 TEST_ASSERT_LESS_THAN_UINT8(20,animation.frame(1000000,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(animation.frame(1000000,PetState::Idle,random),animation.frame(1000000,PetState::Idle,random));
}
void setUp(){}void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(timing);RUN_TEST(wiggle_and_late);return UNITY_END();}
