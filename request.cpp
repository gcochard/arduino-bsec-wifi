#include "request.h"

Request::Request(String &in, bool serial){
    // support VERB PATH and HTTP VERSION to start...
    /* GET / HTTP/1.1
     *  <header0>
     *  ...
     *  <headerN>
     *  
     *  <body>
     */
    
    hasSerial = serial;
    verb = Verb::Invalid;
    path = "/";
    httpVersionMajor = 1;
    httpVersionMinor = 0;
    int verbEnd = in.indexOf(" ");
    if(verbEnd > 8 || verbEnd < 1){
      if(hasSerial) Serial.print("VerbEnd >8 or <1: ");
      if(hasSerial) Serial.println(verbEnd);
      return;
    }
    String sverb = in.substring(0, verbEnd);
    sverb.toLowerCase();
    if(sverb.startsWith("g")){
      verb = Verb::Get;
    } else if(sverb.startsWith("pu")){
      verb = Verb::Put;
    } else if(sverb.startsWith("po")){
      verb = Verb::Post;
    } else if(sverb.startsWith("d")){
      verb = Verb::Delete;
    } else if(sverb.startsWith("h")){
      verb = Verb::Head;
    }
    String rest = in.substring(verbEnd+1);
    String verbStr = "";
    switch(verb){
      case Verb::Get:
      verbStr = "GET";
      break;
      case Verb::Put:
      verbStr = "PUT";
      break;
      case Verb::Post:
      verbStr = "POST";
      break;
      case Verb::Delete:
      verbStr = "DELETE";
      break;
      case Verb::Head:
      verbStr = "HEAD";
      break;
      default:
      verbStr = "Invalid";
    }
    int pathEnd = rest.indexOf(" ");
    if(pathEnd == -1){
      if(hasSerial) Serial.print("PathEnd == -1! ");
      if(hasSerial) Serial.println(rest.indexOf(" "));
      return;
    }
    path = rest.substring(0, pathEnd);
    rest = rest.substring(pathEnd+1);
    String versionstring = rest.substring(0, 8);
    if(!versionstring.startsWith("HTTP")){
      if(hasSerial) Serial.print("versionString not HTTP/X.Y: '");
      if(hasSerial) Serial.print(versionstring);
      if(hasSerial) Serial.println("'");
      return;
    }
    int majorStart = versionstring.indexOf("/")+1;
    httpVersionMajor = versionstring.substring(majorStart,majorStart+1).toInt();
    httpVersionMinor = versionstring.substring(majorStart+2,majorStart+3).toInt();
    if(hasSerial) Serial.println("HTTP request parsed. Verb: " + verbStr + 
                                  "\n\tPath: " + String(path) + 
                                  "\n\tVersion: " + String(httpVersionMajor) + 
                                  "." + String(httpVersionMinor));
    rest = rest.substring(rest.indexOf('\n')+1);
    int headerEnd = rest.indexOf("\n\n");
    String tmp;
    bool hasBody = false;
    if(headerEnd == -1 || headerEnd == rest.length()){
      tmp = rest.substring(0);
    } else {
      tmp = rest.substring(0, headerEnd);
      hasBody = true;
    }
    headerCount = 0;
    if(hasSerial) Serial.println("Parsing headers...");
    while(tmp.indexOf('\n') && headerCount < 20 && tmp.length()){
      headers_arr[headerCount] = new String(tmp.substring(0, tmp.indexOf('\n')));
      //headers_arr[headerCount]->concat(tmp.substring(0,tmp.indexOf('\n')));
      tmp = tmp.substring(tmp.indexOf('\n')+1);
      headerCount++;
    }
    if(tmp.length() > 1){
      if(hasSerial) Serial.println("Warning, unparsed headers ignored:\n" + tmp);
    }
    tmp = rest.substring(headerEnd);
    if(hasBody) {
      body.concat(rest.substring(headerEnd+1));
    }
    if(hasSerial) Serial.println("\tHeaders length: "+String(headerCount));
    if(hasSerial) Serial.println("\tBody length: "+String(body.length()));
}
Request::~Request() {
    for(int i=0;i<headerCount;i++){
      delete headers_arr[i];
    }
}

void Request::setBody(String in){
    body.concat(in);
}

Verb Request::getVerb(){
    return verb;
}
String Request::getPath(){
    return path;
}
String Request::getBody(){
    return body;
}
