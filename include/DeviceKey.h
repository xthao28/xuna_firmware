#pragma once
#include <cstring>
#include <esp32-hal-cpu.h>
#include <esp_random.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>
#include <xuna/identity/DeviceProof.h>
#include "NvsSnapshotStore.h"
namespace xuna {
// Identity V2 device key: ECDSA P-256 (mbedTLS; ESP-IDF 4.4 has no Ed25519).
// The private scalar lives only in NVS `devkey` (32 B) and RAM. Signatures
// are deterministic (RFC 6979). A corrupt stored key is never replaced: that
// would silently change the device identity the cloud pinned (TOFU).
// Known limit: flash encryption is off on the prototype, so a physical
// attacker with USB access can read NVS.
// Heat: the board idles at a low CPU clock; ECDSA runs at full speed so a
// signature does not stall the animation (~3x longer at 80 MHz). The APB
// clock stays 80 MHz either way, so BLE is unaffected.
struct CpuBoost {
  const uint32_t was=getCpuFrequencyMhz();
  CpuBoost(){if(was<240)setCpuFrequencyMhz(240);}
  ~CpuBoost(){if(was<240)setCpuFrequencyMhz(was);}
  CpuBoost(const CpuBoost&)=delete;CpuBoost& operator=(const CpuBoost&)=delete;
};
class DeviceKey: public IDeviceSigner {
  mbedtls_ecp_group group_;
  mbedtls_mpi secret_;
  uint8_t public_[DeviceProof::kPublicKeySize]={};
  bool ready_=false;
  static int random(void*,unsigned char* out,size_t size){esp_fill_random(out,size);return 0;}
 public:
  enum class Result : uint8_t { Loaded,Created,StorageFailed,CorruptKey,CryptoFailed };
  DeviceKey(){mbedtls_ecp_group_init(&group_);mbedtls_mpi_init(&secret_);}
  ~DeviceKey(){mbedtls_mpi_free(&secret_);mbedtls_ecp_group_free(&group_);}
  // Call after BLE init (RF on → esp_fill_random is a true RNG).
  Result begin(NvsSnapshotStore& store){
    const CpuBoost boost;ready_=false;
    if(mbedtls_ecp_group_load(&group_,MBEDTLS_ECP_DP_SECP256R1))return Result::CryptoFailed;
    uint8_t raw[32];Result result=Result::Loaded;
    const auto load=store.readDeviceKey(raw);
    if(load==NvsSnapshotStore::KeyLoad::Corrupt)return Result::CorruptKey;
    if(load==NvsSnapshotStore::KeyLoad::Present){
      if(mbedtls_mpi_read_binary(&secret_,raw,sizeof(raw))||mbedtls_ecp_check_privkey(&group_,&secret_)){
        std::memset(raw,0,sizeof(raw));return Result::CorruptKey;
      }
    }else{
      if(mbedtls_ecp_gen_privkey(&group_,&secret_,random,nullptr)||mbedtls_mpi_write_binary(&secret_,raw,sizeof(raw)))
        return Result::CryptoFailed;
      uint8_t check[32];
      const bool stored=store.writeDeviceKey(raw)&&store.readDeviceKey(check)==NvsSnapshotStore::KeyLoad::Present&&
                        !std::memcmp(raw,check,sizeof(raw));
      std::memset(check,0,sizeof(check));
      if(!stored){std::memset(raw,0,sizeof(raw));return Result::StorageFailed;}
      result=Result::Created;
    }
    std::memset(raw,0,sizeof(raw));
    mbedtls_ecp_point point;mbedtls_ecp_point_init(&point);size_t written=0;
    const bool ok=!mbedtls_ecp_mul(&group_,&point,&secret_,&group_.G,random,nullptr)&&
        !mbedtls_ecp_point_write_binary(&group_,&point,MBEDTLS_ECP_PF_UNCOMPRESSED,&written,public_,sizeof(public_))&&
        written==sizeof(public_);
    mbedtls_ecp_point_free(&point);
    if(!ok)return Result::CryptoFailed;
    ready_=true;return result;
  }
  bool ready() const override { return ready_; }
  const uint8_t* publicKey() const override { return public_; }
  bool sign(const uint8_t digest[32],uint8_t signature[64]) override {
    if(!ready_)return false;
    const CpuBoost boost;
    mbedtls_mpi r,s;mbedtls_mpi_init(&r);mbedtls_mpi_init(&s);
    const bool ok=!mbedtls_ecdsa_sign_det_ext(&group_,&r,&s,&secret_,digest,32,MBEDTLS_MD_SHA256,random,nullptr)&&
        !mbedtls_mpi_write_binary(&r,signature,32)&&!mbedtls_mpi_write_binary(&s,signature+32,32);
    mbedtls_mpi_free(&r);mbedtls_mpi_free(&s);return ok;
  }
};
}
