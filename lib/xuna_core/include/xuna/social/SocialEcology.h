#pragma once
#include <cstdint>
#include <cstring>
#include <xuna/visual/SceneEffects.h>
#include <xuna/visual/SymbioticRoom.h>
#include <xuna/visual/VariantPlayer.h>

namespace xuna {

// SYM-4 S4.1: XUNA-to-XUNA Social Ecology (8-byte BLE Proximity Beacon &
// Temperament-Driven Encounter Engine).
// Packet layout (8 bytes, U0-safe: no owner PII):
//   [0] = 0x58 ('X')
//   [1] = 0x53 ('S')
//   [2..5] = shortPetId (first 4 bytes of 16-byte PetId)
//   [6] = (dominantTrait & 0x0F) | ((uint8_t(tint) & 0x0F) << 4)
//   [7] = 0x80 | ((uint8_t(sharedTrick) & 0x07) << 3) | (moodCode & 0x07)
struct SocialBeaconPacket {
  static constexpr uint8_t kSize = 8;
  static constexpr uint8_t kMagic0 = 0x58;  // 'X'
  static constexpr uint8_t kMagic1 = 0x53;  // 'S'

  uint8_t shortPetId[4] = {0, 0, 0, 0};
  uint8_t dominantTrait = 0;
  EpigeneticTint tint = EpigeneticTint::Original;
  RhythmTrickId sharedTrick = RhythmTrickId::None;
  uint8_t moodCode = 0;

  void encode(uint8_t out[kSize]) const {
    out[0] = kMagic0;
    out[1] = kMagic1;
    out[2] = shortPetId[0];
    out[3] = shortPetId[1];
    out[4] = shortPetId[2];
    out[5] = shortPetId[3];
    out[6] = uint8_t((dominantTrait & 0x0Fu) |
                     ((static_cast<uint8_t>(tint) & 0x0Fu) << 4));
    out[7] = uint8_t(0x80u |
                     ((static_cast<uint8_t>(sharedTrick) & 0x07u) << 3) |
                     (moodCode & 0x07u));
  }

  static bool decode(const uint8_t* raw, uint8_t len, SocialBeaconPacket& out) {
    if (!raw || len < kSize) return false;
    if (raw[0] != kMagic0 || raw[1] != kMagic1) return false;
    if ((raw[7] & 0x80u) == 0) return false;
    // Reject all-zero shortPetId.
    if ((raw[2] | raw[3] | raw[4] | raw[5]) == 0) return false;
    out.shortPetId[0] = raw[2];
    out.shortPetId[1] = raw[3];
    out.shortPetId[2] = raw[4];
    out.shortPetId[3] = raw[5];
    out.dominantTrait = uint8_t(raw[6] & 0x0Fu);
    if (out.dominantTrait > 4) out.dominantTrait = 0;
    const uint8_t rawTint = uint8_t((raw[6] >> 4) & 0x0Fu);
    out.tint = rawTint <= 5 ? static_cast<EpigeneticTint>(rawTint)
                            : EpigeneticTint::Original;
    const uint8_t rawTrick = uint8_t((raw[7] >> 3) & 0x07u);
    out.sharedTrick = rawTrick <= 3 ? static_cast<RhythmTrickId>(rawTrick)
                                    : RhythmTrickId::None;
    out.moodCode = uint8_t(raw[7] & 0x07u);
    return true;
  }
};

enum class SocialGreetingKind : uint8_t {
  None = 0,
  HarmoniousSpark = 1,  // Curious / Playful Resonance
  WarmNuzzle = 2,       // Sociable / Tempo Resonance
  ShyPeek = 3,          // Sensitive / Gentle Cautious Peek
};

struct SocialEncounterRecord {
  uint8_t peerShortId[4] = {0, 0, 0, 0};
  uint8_t peerTrait = 0;
  EpigeneticTint peerTint = EpigeneticTint::Original;
  RhythmTrickId sharedTrick = RhythmTrickId::None;
  SocialGreetingKind greeting = SocialGreetingKind::None;
  int8_t rssi = -127;
  uint32_t atMs = 0;
};

class SocialEncounterEngine {
 public:
  static constexpr int8_t kMinEncounterRssiDbm = -65;
  static constexpr uint32_t kPeerCooldownMs = 300000;  // 5 minutes per peer
  static constexpr uint8_t kMaxHistory = 4;

  // Evaluates an incoming 8-byte beacon packet against our own PetId and
  // dominant trait. Returns true and populates `outRecord` if a new proximity
  // encounter occurred (`rssi >= -65 dBm`, distinct PetId, outside cooldown).
  bool onBeaconObserved(const uint8_t* myPetId16,
                        uint8_t myDominantTrait,
                        const uint8_t* rawBeacon,
                        uint8_t rawLen,
                        int8_t rssi,
                        uint32_t now,
                        SocialEncounterRecord& outRecord) {
    if (!myPetId16 || rssi < kMinEncounterRssiDbm) return false;
    SocialBeaconPacket pkt;
    if (!SocialBeaconPacket::decode(rawBeacon, rawLen, pkt)) return false;

    // Ignore our own beacon.
    if (pkt.shortPetId[0] == myPetId16[0] &&
        pkt.shortPetId[1] == myPetId16[1] &&
        pkt.shortPetId[2] == myPetId16[2] &&
        pkt.shortPetId[3] == myPetId16[3]) {
      return false;
    }

    // Enforce per-peer cooldown.
    for (uint8_t i = 0; i < count_; ++i) {
      const SocialEncounterRecord& prev = history_[i];
      if (prev.peerShortId[0] == pkt.shortPetId[0] &&
          prev.peerShortId[1] == pkt.shortPetId[1] &&
          prev.peerShortId[2] == pkt.shortPetId[2] &&
          prev.peerShortId[3] == pkt.shortPetId[3]) {
        if (uint32_t(now - prev.atMs) < kPeerCooldownMs) {
          return false;
        }
      }
    }

    const SocialGreetingKind greeting =
        greetingForTraits(myDominantTrait, pkt.dominantTrait);
    SocialEncounterRecord rec;
    rec.peerShortId[0] = pkt.shortPetId[0];
    rec.peerShortId[1] = pkt.shortPetId[1];
    rec.peerShortId[2] = pkt.shortPetId[2];
    rec.peerShortId[3] = pkt.shortPetId[3];
    rec.peerTrait = pkt.dominantTrait;
    rec.peerTint = pkt.tint;
    rec.sharedTrick = pkt.sharedTrick;
    rec.greeting = greeting;
    rec.rssi = rssi;
    rec.atMs = now;

    history_[head_] = rec;
    head_ = uint8_t((head_ + 1u) % kMaxHistory);
    if (count_ < kMaxHistory) ++count_;
    ++totalEncounters_;
    outRecord = rec;
    return true;
  }

  static SocialGreetingKind greetingForTraits(uint8_t myTrait,
                                              uint8_t peerTrait) {
    // Trait indices: 0=Curiosity, 1=Sociability, 2=Playfulness, 3=Tempo, 4=Sensitivity
    if (myTrait == 4 || peerTrait == 4) {
      return SocialGreetingKind::ShyPeek;
    }
    if (myTrait == 0 || myTrait == 2 || peerTrait == 0 || peerTrait == 2) {
      return SocialGreetingKind::HarmoniousSpark;
    }
    return SocialGreetingKind::WarmNuzzle;
  }

  static VisualVariant variantFor(SocialGreetingKind kind) {
    switch (kind) {
      case SocialGreetingKind::HarmoniousSpark: return VisualVariant::BouncePlay;
      case SocialGreetingKind::WarmNuzzle: return VisualVariant::GreetNuzzle;
      case SocialGreetingKind::ShyPeek: return VisualVariant::LookAround;
      case SocialGreetingKind::None: break;
    }
    return VisualVariant::None;
  }

  static EffectKind effectFor(SocialGreetingKind kind) {
    switch (kind) {
      case SocialGreetingKind::HarmoniousSpark: return EffectKind::Sparkle;
      case SocialGreetingKind::WarmNuzzle: return EffectKind::Heart;
      case SocialGreetingKind::ShyPeek: return EffectKind::Sparkle;
      case SocialGreetingKind::None: break;
    }
    return EffectKind::None;
  }

  static const char* speechFor(SocialGreetingKind kind) {
    switch (kind) {
      case SocialGreetingKind::HarmoniousSpark:
        return "Gặp bạn XUNA mới! Tụi mình vừa trao đổi chiêu trò vui lắm!";
      case SocialGreetingKind::WarmNuzzle:
        return "Chào bạn XUNA hàng xóm! Rất vui được làm quen nè!";
      case SocialGreetingKind::ShyPeek:
        return "Ngó nghiêng... có một bạn XUNA đang ở ngay cạnh mình kìa!";
      case SocialGreetingKind::None:
        break;
    }
    return "";
  }

  uint8_t count() const { return count_; }
  uint16_t totalEncounters() const { return totalEncounters_; }
  const SocialEncounterRecord& record(uint8_t index) const {
    return history_[index < kMaxHistory ? index : 0];
  }

 private:
  SocialEncounterRecord history_[kMaxHistory] = {};
  uint8_t head_ = 0;
  uint8_t count_ = 0;
  uint16_t totalEncounters_ = 0;
};

}  // namespace xuna

