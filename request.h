#if !defined request_h
#define request_h
#include "Arduino.h"

enum class Verb { Get, Put, Post, Delete, Head, Invalid };

class Request{
  public:
    Request(String &in, bool serial);
    ~Request();
  
    void setBody(String in);
  
    Verb getVerb();
    String getPath();
    String getBody();
    String* getHeaders();
  
  private:
    bool hasSerial;
    Verb verb;
    String path;
    int httpVersionMajor;
    int httpVersionMinor;
    String* headers_arr[20];
    int headerCount;
    String body;
};
#endif
