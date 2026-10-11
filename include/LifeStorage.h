#pragma once
#include <Preferences.h>
#include "LifeEngine.h"
namespace xuna {
class LifeStorage {
  Preferences prefs_;
  bool ready_=false;
 public:
  bool begin(LifeEngine& life) {
    ready_=prefs_.begin("xuna_life",false);if(!ready_)return false;
    uint8_t bytes[2][LifeEngine::kSnapshotSize]={};size_t sizes[2]={};
    const bool exists[2]={prefs_.isKey("slot0"),prefs_.isKey("slot1")};
    for(int i=0;i<2;++i){const char* key=i?"slot1":"slot0";if(exists[i]&&prefs_.getBytesLength(key)==sizeof(bytes[i]))sizes[i]=prefs_.getBytes(key,bytes[i],sizeof(bytes[i]));}
    if(!life.recover(bytes[0],sizes[0],bytes[1],sizes[1])&&(exists[0]||exists[1]))Serial.println("WARNING: life snapshots invalid; starting clean");
    return true;
  }
  bool save(LifeEngine& life) {
    if(!ready_)return false;
    uint8_t bytes[LifeEngine::kSnapshotSize];++life.generation;life.encode(bytes);
    return prefs_.putBytes(life.generation%2?"slot1":"slot0",bytes,sizeof(bytes))==sizeof(bytes);
  }
  bool clear(LifeEngine& life){
    if(!ready_)return false;
    // Write a newer empty snapshot to both slots: old memories cannot recover
    // on subsequent corruption of only one slot.
    LifeEngine empty;empty.generation=life.generation;empty.activeSeconds=life.activeSeconds;
    if(!save(empty))return false;
    // Commit runtime reset once a durable empty generation exists. A failure
    // writing the second slot is reported, never silently claimed successful.
    life=empty;
    if(!save(empty))return false;
    life=empty;return true;
  }
};
}
