#pragma once
#include <cstdint>

namespace xuna {
namespace hid {

class HidLinkRoles {
 public:
  static constexpr uint8_t kMaxLinks = 2;
  static constexpr uint16_t kNoConnId = 0xFFFF;
  static constexpr uint32_t kSquatterTimeoutMs = 30000;

  struct Link {
    bool used = false;
    uint16_t connId = kNoConnId;
    bool appAccess = false;
    bool hidHost = false;
    uint32_t connectedAt = 0;
  };

  struct Action {
    bool refuse = false;
    bool disconnect = false;
    uint16_t disconnectConnId = kNoConnId;
    bool sessionBump = false;
    bool appConnected = false;
    bool hidConnected = false;
    bool advertise = true;
  };

  Action onConnect(uint16_t id, uint32_t now) {
    if (find(id) != nullptr) return makeAction();
    int freeSlot = -1;
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (!links_[i].used) {
        freeSlot = int(i);
        break;
      }
    }
    if (freeSlot < 0) {
      Action a = makeAction();
      a.refuse = true;
      a.disconnect = true;
      a.disconnectConnId = id;
      return a;
    }
    Link& slot = links_[freeSlot];
    slot.used = true;
    slot.connId = id;
    slot.appAccess = false;
    slot.hidHost = false;
    slot.connectedAt = now;
    return makeAction();
  }

  Action onAppAccess(uint16_t id) {
    Link* target = find(id);
    if (!target) return makeAction();
    if (target->appAccess) return makeAction();
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used && links_[i].connId != id && links_[i].appAccess) {
        remove(id);
        Action a = makeAction();
        a.disconnect = true;
        a.disconnectConnId = id;
        return a;
      }
    }
    target->appAccess = true;
    Action a = makeAction();
    a.sessionBump = true;
    return a;
  }

  Action onEncrypted(uint16_t id, bool bonded) {
    Link* target = find(id);
    if (!target || !bonded) return makeAction();
    if (target->hidHost) return makeAction();
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used && links_[i].connId != id && links_[i].hidHost) {
        const bool wasApp = target->appAccess;
        remove(id);
        Action a = makeAction();
        a.disconnect = true;
        a.disconnectConnId = id;
        a.sessionBump = wasApp;
        return a;
      }
    }
    target->hidHost = true;
    return makeAction();
  }

  Action onDisconnect(uint16_t id) {
    Link* target = find(id);
    if (!target) return makeAction();
    const bool wasApp = target->appAccess;
    *target = Link{};
    Action a = makeAction();
    a.sessionBump = wasApp;
    return a;
  }

  Action tick(uint32_t now) {
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      Link& l = links_[i];
      if (!l.used || l.appAccess || l.hidHost) continue;
      if (uint32_t(now - l.connectedAt) >= kSquatterTimeoutMs) {
        const uint16_t id = l.connId;
        l = Link{};
        Action a = makeAction();
        a.disconnect = true;
        a.disconnectConnId = id;
        return a;
      }
    }
    return makeAction();
  }

  bool appConnected() const { return appConnId() != kNoConnId; }
  bool hidConnected() const { return hidConnId() != kNoConnId; }

  uint16_t appConnId() const {
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used && links_[i].appAccess) return links_[i].connId;
    }
    return kNoConnId;
  }

  uint16_t hidConnId() const {
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used && links_[i].hidHost) return links_[i].connId;
    }
    return kNoConnId;
  }

  uint8_t linkCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used) ++count;
    }
    return count;
  }

  bool shouldAdvertise() const { return linkCount() < kMaxLinks; }

 private:
  Link links_[kMaxLinks] = {};

  Link* find(uint16_t id) {
    for (uint8_t i = 0; i < kMaxLinks; ++i) {
      if (links_[i].used && links_[i].connId == id) return &links_[i];
    }
    return nullptr;
  }

  void remove(uint16_t id) {
    Link* l = find(id);
    if (l) *l = Link{};
  }

  Action makeAction() const {
    Action a;
    a.appConnected = appConnected();
    a.hidConnected = hidConnected();
    a.advertise = shouldAdvertise();
    return a;
  }
};

}  // namespace hid
}  // namespace xuna

