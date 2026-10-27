#pragma once
#include <cstddef>
#include <cstdint>
namespace xuna {
// Fixed-bucket latency histogram for long board runs (E14): O(1) add, no
// allocation, percentiles at bucket resolution. Bucket i holds samples in
// [i*W, (i+1)*W) µs; the last bucket also takes everything above.
template<size_t Buckets,uint32_t BucketUs> class LatencyHistogram {
  static_assert(Buckets>1&&BucketUs>0,"LatencyHistogram needs buckets");
 public:
  void add(uint32_t us) {
    size_t i=us/BucketUs;if(i>=Buckets)i=Buckets-1;
    ++counts_[i];++count_;sum_+=us;if(us>max_)max_=us;
  }
  uint32_t count() const {return count_;}
  uint32_t maxUs() const {return max_;}
  uint32_t meanUs() const {return count_?uint32_t(sum_/count_):0;}
  // Upper edge of the bucket holding the q-th per-mille sample (q 1..1000),
  // capped by the true maximum; 0 when empty.
  uint32_t percentileUs(uint16_t perMille) const {
    if(!count_)return 0;
    if(perMille>1000)perMille=1000;
    const uint64_t rank=(uint64_t(count_)*perMille+999)/1000;
    uint64_t seen=0;
    for(size_t i=0;i<Buckets;++i){
      seen+=counts_[i];
      if(seen>=rank&&seen){
        if(i==Buckets-1)return max_;  // overflow bucket has no upper edge
        const uint32_t edge=uint32_t((i+1)*BucketUs);return edge<max_?edge:max_;}
    }
    return max_;
  }
  void clear(){for(size_t i=0;i<Buckets;++i)counts_[i]=0;count_=0;sum_=0;max_=0;}
 private:
  uint32_t counts_[Buckets]={};uint32_t count_=0,max_=0;uint64_t sum_=0;
};
}
