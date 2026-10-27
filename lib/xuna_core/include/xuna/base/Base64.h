#pragma once
#include <cstddef>
#include <cstdint>
namespace xuna {
// Standard base64 (RFC 4648, with padding) for the few binary fields of the
// device-direct JSON frames (nonce, signature).
struct Base64 {
  static constexpr size_t encodedSize(size_t n){return (n+2)/3*4;}
  // Writes encodedSize(n) chars plus NUL; false if `cap` is too small.
  static bool encode(const uint8_t* in,size_t n,char* out,size_t cap) {
    static const char kAlphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if(cap<encodedSize(n)+1)return false;
    size_t o=0;
    for(size_t i=0;i<n;i+=3){
      const uint32_t v=(uint32_t(in[i])<<16)|(i+1<n?uint32_t(in[i+1])<<8:0)|(i+2<n?in[i+2]:0);
      out[o++]=kAlphabet[(v>>18)&63];out[o++]=kAlphabet[(v>>12)&63];
      out[o++]=i+1<n?kAlphabet[(v>>6)&63]:'=';out[o++]=i+2<n?kAlphabet[v&63]:'=';
    }
    out[o]=0;return true;
  }
  // Decodes exactly `expected` bytes from `len` chars; false otherwise.
  static bool decode(const char* in,size_t len,uint8_t* out,size_t expected) {
    if(len%4||len!=encodedSize(expected))return false;
    const size_t padding=len&&in[len-1]=='='?(in[len-2]=='='?2:1):0;
    if(len/4*3-padding!=expected)return false;  // e.g. "Zm9v" holds 3 bytes, not 2
    size_t o=0;
    for(size_t i=0;i<len;i+=4){
      int v[4];
      for(int k=0;k<4;++k){
        v[k]=value(in[i+k]);
        if(v[k]<0&&!(in[i+k]=='='&&i+4==len&&k>=2))return false;
      }
      if(in[i+2]=='='&&in[i+3]!='=')return false;
      const uint32_t w=(uint32_t(v[0])<<18)|(uint32_t(v[1])<<12)|(uint32_t(v[2]<0?0:v[2])<<6)|uint32_t(v[3]<0?0:v[3]);
      const uint8_t bytes[3]={uint8_t(w>>16),uint8_t(w>>8),uint8_t(w)};
      for(int k=0;k<3&&o<expected;++k){
        if(k>0&&v[k+1]<0)break;
        out[o++]=bytes[k];
      }
    }
    return o==expected;
  }
 private:
  static int value(char c) {
    if(c>='A'&&c<='Z')return c-'A';
    if(c>='a'&&c<='z')return c-'a'+26;
    if(c>='0'&&c<='9')return c-'0'+52;
    if(c=='+')return 62;
    if(c=='/')return 63;
    return -1;
  }
};
}
