#pragma once
#include <cstdint>
#include <limits>
namespace xuna {
// Unsigned fixed point, 16 fractional bits. Arithmetic saturates and rounds half up.
// Division by zero: 0/0 -> 0, positive/0 -> maximum (explicit bounded sentinel).
template<class Storage> class Fixed {
 public:
  static Fixed fromRaw(Storage raw) { return Fixed(raw); }
  static Fixed fromInteger(uint32_t value) { return limited(uint64_t(value)<<16); }
  static Fixed fromRatio(uint32_t numerator,uint32_t denominator) {
    if(!denominator)return limited(numerator?maximum():0);
    return limited(((uint64_t(numerator)<<16)+denominator/2)/denominator);
  }
  Storage raw() const { return value_; }
  Fixed multiply(Fixed other) const { return limited((uint64_t(value_)*other.value_+32768)>>16); }
  Fixed divide(Fixed other) const {
    if(!other.value_)return limited(value_?maximum():0);
    return limited(((uint64_t(value_)<<16)+other.value_/2)/other.value_);
  }
  Fixed add(Fixed other) const { return limited(uint64_t(value_)+other.value_); }
  Fixed subtract(Fixed other) const { return fromRaw(value_>other.value_?value_-other.value_:0); }
  // Caller supplies ordered bounds.
  Fixed clamp(Fixed low,Fixed high) const { return value_<low.value_?low:(value_>high.value_?high:*this); }
 private:
  explicit Fixed(Storage raw):value_(raw) {}
  static uint64_t maximum() { return std::numeric_limits<Storage>::max(); }
  static Fixed limited(uint64_t value) { return Fixed(static_cast<Storage>(value>maximum()?maximum():value)); }
  Storage value_;
};
using Q0_16=Fixed<uint16_t>; // maximum < 1 (65535/65536)
using Q16_16=Fixed<uint32_t>;
}
