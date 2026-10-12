#pragma once
#include <cstddef>
namespace xuna {
// Single-owner buffer. Full push rejects new input; never overwrites older entries.
template<class T,size_t N> class RingBuffer {
  static_assert(N>0,"RingBuffer requires positive capacity");
 public:
  bool push(const T& value) {
    if(count_==N)return false;
    values_[(head_+count_)%N]=value;++count_;return true;
  }
  bool pop(T& value) {
    if(!count_)return false;
    value=values_[head_];head_=(head_+1)%N;--count_;return true;
  }
  size_t size() const { return count_; }
  bool empty() const { return count_==0; }
  void clear() { head_=count_=0; }
 private:
  T values_[N]={};size_t head_=0,count_=0;
};
}
