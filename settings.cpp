#include "settings.h"

Settings::Settings(){
  hasSerial = false;
}
Settings::Settings(bool serial){
  hasSerial = serial;
}
void Settings::setHasSerial(bool serial){
  hasSerial = serial;
}

void Settings::print(String val){
  if(hasSerial){
    Serial.print(val);
  }
}
void Settings::println(String val){
  if(hasSerial){
    print(val+"\n");
  }
}
void Settings::print(char *val){
  if(hasSerial){
    Serial.print(val);
  }
}

void Settings::getWifiSettings(String& currentSsid, String& currentPass) {
  currentSsid.remove(0);
  currentPass.remove(0);
  LittleFS.begin();
  File f = LittleFS.open("/wifi.txt", "r");
  if(f) {
    // only read if open was successful
    // debug, read the whole file and dump it to serial
    println("Opened wifi.txt!");
    // get the values we need
    String newssid = f.readStringUntil('\n');
    newssid.trim();
    String newpass = f.readStringUntil('\n');
    newpass.trim();
    currentSsid.concat(newssid);
    currentPass.concat(newpass);
    f.close();
  } else {
    println("No wifi.txt!");
    // get the default from headers
    currentSsid.concat(ssid);
    currentPass.concat(pass);
  }
  LittleFS.end();
}

void Settings::setWifiSettings(String newSsid, String newPass, String& currentSsid, String& currentPass) {
  // set the currentSsid and currentPass to the supplied values
  currentSsid.remove(0);
  currentSsid.concat(newSsid);
  currentPass.remove(0);
  currentPass.concat(newPass);
  // then commit it to the filesystem
  LittleFS.begin();
  File f = LittleFS.open("/wifi.txt", "w");
  if(f) {
    println("Opened wifi.txt!");
    // only write if open was successful
    int ssidLen = f.println(newSsid);
    int passLen = f.println(newPass);
    f.close();
  }
  LittleFS.end();
}

void Settings::getLocation(String& currentLocation) {
  currentLocation.remove(0);
  LittleFS.begin();
  File f = LittleFS.open("/location.txt", "r");
  if(f) {
    println("Opened location.txt!");
    // only read if open was successful
    String newloc = f.readStringUntil('\n');
    newloc.trim();
    currentLocation.concat(newloc);
    println(currentLocation);
    f.close();
  } else {
    // get the default from headers
    currentLocation.concat(defaultMetricLocation);
  }
  LittleFS.end();
}

void Settings::setLocation(String newLocation, String& currentLocation) {
  currentLocation.remove(0);
  currentLocation.concat(newLocation);
  LittleFS.begin();
  File f = LittleFS.open("/location.txt", "w");
  if(f) {
    // only write if open was successful
    int locLen = f.println(newLocation);
    f.close();
  }
  LittleFS.end();
}
