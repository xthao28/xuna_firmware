#include <cstdio>
#include <xuna/learning/LearnerVocabulary.h>
using namespace xuna;
static uint32_t q16(double v){return static_cast<uint32_t>(v*65536.0+0.5);}
int main(){
 const double ws[]={.25,.5,.75,1.0}, caps[]={1,2,3};
 std::puts("weight,cap,seeds,stated_mass,ratings_to_override,overridden_all");
 for(double w:ws)for(double cap:caps){uint32_t total=0,ok=0,mass=0;
  for(uint32_t seed=1;seed<=200;++seed){LearnerV3Config c=productLearnerConfig();
   c.policy=ExplorePolicy::Greedy;c.statedWeightQ16=q16(w);c.statedCapQ16=q16(cap);
   PreferenceLearnerV3 l(c);for(int i=0;i<20;++i)l.state(0,0,1,true);
   mass=l.view(0,0,1).statedPositiveQ16;uint32_t n=0;
   for(;n<10&&l.greedy(0,0)==1;++n){V3Decision d;d.id=seed*100+n+1;
    d.interaction=0;d.action=1;d.predictedQ16=l.view(0,0,1).pQ16;l.feedback(d,false);}
   if(l.greedy(0,0)!=1)++ok;
   total+=n;}
  std::printf("%.2f,%.2f,200,%.4f,%.3f,%s\n",w,cap,mass/65536.0,
              total/200.0,ok==200?"true":"false");}
}
