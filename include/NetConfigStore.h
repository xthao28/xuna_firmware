#pragma once
#include <Preferences.h>
#include <xuna/net/NetConfig.h>
namespace xuna {
// Wi-Fi and server settings (DC-2): their own NVS namespace `xuna_net`, never
// `xuna_life`. Written only when the owner sets or forgets them in the app;
// forget erases the namespace. Values are never logged.
class NetConfigStore {
  static constexpr const char* kNamespace="xuna_net";
 public:
  bool load(NetConfig& out) {
    out.wipe();
    Preferences prefs;
    if(!prefs.begin(kNamespace,true))return false;  // never configured
    prefs.getString("ssid",out.ssid,sizeof(out.ssid));
    prefs.getString("pass",out.pass,sizeof(out.pass));
    prefs.getString("host",out.host,sizeof(out.host));
    out.port=prefs.getUShort("port",0);
    prefs.end();
    if(!out.valid()){out.wipe();return false;}
    return true;
  }
  bool save(const NetConfig& c) {
    Preferences prefs;
    if(!prefs.begin(kNamespace,false))return false;
    const bool ok=prefs.putString("ssid",c.ssid)==std::strlen(c.ssid)&&
                  prefs.putString("pass",c.pass)==std::strlen(c.pass)&&
                  prefs.putString("host",c.host)==std::strlen(c.host)&&prefs.putUShort("port",c.port)==2;
    prefs.end();
    return ok;
  }
  bool erase() {
    Preferences prefs;
    if(!prefs.begin(kNamespace,false))return false;
    const bool ok=prefs.clear();
    prefs.end();
    return ok;
  }
};
}
