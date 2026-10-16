#include <unity.h>
#include <xuna/visual/PetVisualAnimation.h>
using namespace xuna;
struct ZeroRandom:IRandom{uint32_t next() override{return 0;}};
void timing(){ZeroRandom random;PetVisualAnimation animation;animation.begin(0,random);
 TEST_ASSERT_EQUAL_UINT8(0,animation.frame(0,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(9,animation.frame(900,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(4,animation.frame(3600,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(32,animation.frame(3600,PetState::Happy,random));
 TEST_ASSERT_EQUAL_UINT8(33,animation.frame(3780,PetState::Happy,random));
 TEST_ASSERT_EQUAL_UINT8(64,animation.frame(3780,PetState::Curious,random));
 TEST_ASSERT_EQUAL_UINT8(68,animation.frame(4230,PetState::Curious,random));
 TEST_ASSERT_EQUAL_UINT8(96,animation.frame(4230,PetState::Sleep,random));
 TEST_ASSERT_EQUAL_UINT8(108,animation.frame(5430,PetState::Sleep,random));
}
void rollover_and_late(){ZeroRandom random;PetVisualAnimation animation;
 animation.begin(UINT32_MAX-100,random);
 TEST_ASSERT_EQUAL_UINT8(9,animation.frame(799,PetState::Idle,random));
 const auto frame=animation.frame(1000000,PetState::Idle,random);
 TEST_ASSERT_LESS_THAN_UINT8(128,frame);
 TEST_ASSERT_EQUAL_UINT8(frame,animation.frame(1000000,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(96,animation.frame(1000000,PetState::Sleep,random));
 TEST_ASSERT_EQUAL_UINT8(96,animation.frame(1003200,PetState::Sleep,random));
}
void setUp(){}void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(timing);RUN_TEST(rollover_and_late);return UNITY_END();}
