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
// Personality tempo (PE-2): a new period continues from the current frame.
void interval_change_keeps_the_frame(){ZeroRandom random;PetVisualAnimation animation;animation.begin(0,random);
 TEST_ASSERT_EQUAL_UINT8(9,animation.frame(950,PetState::Idle,random));
 animation.setInterval(85,950);
 TEST_ASSERT_EQUAL_UINT16(85,animation.interval());
 TEST_ASSERT_EQUAL_UINT8(9,animation.frame(950,PetState::Idle,random));
 TEST_ASSERT_EQUAL_UINT8(10,animation.frame(950+85,PetState::Idle,random));
 animation.setInterval(0,2000);animation.setInterval(85,2000);  // ignored / unchanged
 TEST_ASSERT_EQUAL_UINT16(85,animation.interval());
 TEST_ASSERT_EQUAL_UINT8(21,animation.frame(950+85*12,PetState::Idle,random));
}
void reaction_length_is_settable(){AnimationManager a;a.begin(0);
 a.setState(PetState::Happy,0);a.update(2400);
 TEST_ASSERT_EQUAL_UINT8(uint8_t(PetState::Idle),uint8_t(a.state()));  // default 2400 ms
 a.setReactionMs(2760);a.setState(PetState::Curious,3000);
 a.update(5400);TEST_ASSERT_EQUAL_UINT8(uint8_t(PetState::Curious),uint8_t(a.state()));
 a.update(5760);TEST_ASSERT_EQUAL_UINT8(uint8_t(PetState::Idle),uint8_t(a.state()));
}
void setUp(){}void tearDown(){}
int main(){UNITY_BEGIN();RUN_TEST(timing);RUN_TEST(rollover_and_late);RUN_TEST(interval_change_keeps_the_frame);
 RUN_TEST(reaction_length_is_settable);return UNITY_END();}
