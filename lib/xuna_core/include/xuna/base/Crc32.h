#pragma once
#include <cstdint>
#include <cstddef>
namespace xuna {
// CRC-32/ISO-HDLC (IEEE reflected), check "123456789" = 0xCBF43926.
inline uint32_t crc32(const uint8_t* bytes,size_t count) {
 uint32_t crc=UINT32_MAX;
 for(size_t i=0;i<count;++i){crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xEDB88320U:0U);}
 return ~crc;
}
}
