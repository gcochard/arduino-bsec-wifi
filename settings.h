#if !defined settings_h
#define settings_h
#include <LittleFS.h>
#include "secrets.h"

const unsigned long connectionTimeout = 20L * 1000L;
const int SSID_MAX = 32;
const int PASS_MAX = 64;
const int LOCATION_MAX = 32;

class Settings {

public:
  Settings();
  Settings(bool);
  ~Settings();
  void setHasSerial(bool);
  /**
   * Functions to read and write wifi settings from the filesystem
   */
  void getWifiSettings(String& currentSsid, String& currentPass);
  void setWifiSettings(String newSsid, String newPass, String& currentSsid, String& currentPass);
    /**
   * Functions to read and write location settings from the filesystem
   */
  void getLocation(String& currentLocation);
  void setLocation(String newLocation, String& currentLocation);
private:
  bool hasSerial;
  void println(String);
  void print(String);
  void print(char*);
};
#endif
