/**
 * Copyright (C) 2021 Bosch Sensortec GmbH
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * 
 */

/* If compiling this examples leads to an 'undefined reference error', refer to the README
 * at https://github.com/BoschSensortec/Bosch-BSEC2-Library
 */
/* The new sensor needs to be conditioned before the example can work reliably. You may run this
 * example for 24hrs to let the sensor stabilize.
 */

/**
 * basic_config_state.ino sketch :
 * This is an example for integration of BSEC2x library using configuration setting and has been 
 * tested with Adafruit ESP8266 Board
 * 
 * For quick integration test, example code can be used with configuration file under folder
 * Bosch_BSEC2_Library/src/config/FieldAir_HandSanitizer (Configuration file added as simple
 * code example for integration but not optimized on classification performance)
 * Config string for H2S and NonH2S target classes is also kept for reference (Suitable for
 * lab-based characterization of the sensor)
 */

/* Use the Espressif EEPROM library. Skip otherwise */
#if defined(ARDUINO_ARCH_ESP32) || (ARDUINO_ARCH_ESP8266) || (ARDUINO_PICO_MAJOR)
#include <EEPROM.h>
#define USE_EEPROM
#endif
#include <WiFi.h>
#include <WiFiClass.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

/* oled screen stuff */
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library. 
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool hasDisplay = false;
/* end oled screen stuff */

bool hasSerial = false;
bool wifiApMode = false;



// Enter your WiFi SSID and password
#include "secrets.h"
char apSSID[] = "rooty-BSEC";         // fallback AP-mode SSID

int status = WL_IDLE_STATUS;
#include <bsec2.h>
/* Configuration for two class classification used here
 * For four class classification please use configuration under config/FieldAir_HandSanitizer_Onion_Cinnamon
 */
#include "config/FieldAir_HandSanitizer/FieldAir_HandSanitizer.h"

/* Macros used */
#define STATE_SAVE_PERIOD UINT32_C(360 * 60 * 1000) /* 360 minutes - 4 times a day */
#define PANIC_LED LED_BUILTIN
#define ERROR_DUR 1000
const unsigned long connectionTimeout = 10L * 1000L;

/* Helper functions declarations */

/**
 * Display a spinner in the top right of the display
 */
void spin(void);

/**
 * @brief : This function toggles the led continuously with one second delay
 */
void errLeds(int, int);

/**
 * Print the status of the wifi connection to serial
 */
void printWifiStatus(void);

/**
 * @brief : This function checks the BSEC status, prints the respective error code. Halts in case of error
 * @param[in] bsec  : Bsec2 class object
 */
void checkBsecStatus(Bsec2 bsec);

/**
 * @brief : This function updates/saves BSEC state
 * @param[in] bsec  : Bsec2 class object
 */
void updateBsecState(Bsec2 bsec);

/**
 * Display the latest measurement data along with wifi status
 */
void updateDisplay(void);

/**
 * Process an HTTP request if there is one pending
 */
bool processOneRequest(WiFiClient&);

/**
 * @brief : This function is called by the BSEC library when a new output is available
 * @param[in] input     : BME68X sensor data before processing
 * @param[in] outputs   : Processed BSEC BSEC output data
 * @param[in] bsec      : Instance of BSEC2 calling the callback
 */
void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec);

/**
 * @brief : This function retrieves the existing state
 * @param : Bsec2 class object
 */
bool loadState(Bsec2 bsec);

/**
 * @brief : This function writes the state into EEPROM
 * @param : Bsec2 class object
 */
bool saveState(Bsec2 bsec);

/* Create an object of the class Bsec2 */
Bsec2 envSensor;
#ifdef USE_EEPROM
static uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
#endif
/* Gas estimate names will be according to the configuration classes used */
const String gasName[] = { "Field Air", "Hand sanitizer", "Undefined 3", "Undefined 4"};

WiFiServer server(80);

#ifdef TLS
#include <WiFiServerSecure.h>
WiFiServerSecure tlsserver(443);
ServerSessions serverCache(5);
#endif


void connectToWifi(){
  // attempt to connect to Wifi network:
  if(hasSerial) Serial.print("Attempting to connect to SSID: ");
  if(hasSerial) Serial.println(ssid);

  unsigned long startTime = millis();
  WiFi.begin(ssid, pass);
  wifiApMode = false;
  while (WiFi.status() != WL_CONNECTED) {
      spin();
      delay(500);
      if(hasSerial) Serial.print(".");
      if(hasDisplay){
        display.print(".");
        display.display();
      }
      if(millis() - startTime > connectionTimeout){
        wifiApMode = true;
        WiFi.beginAP(apSSID);
        break;
      }
  }

}

enum class Verb { Get, Put, Post, Delete, Head, Invalid };

typedef struct
{
  float raw_temp;
  float raw_pressure;
  float raw_humidity;
  float raw_gas;
  float raw_gas_index;
  float comp_temp;
  float comp_temp_c;
  float comp_humidity;
  float run_in_status;
  float stabilization_status;
  float gas_estimate_1;
  float gas_estimate_2;
  float gas_estimate_3;
  float gas_estimate_4;
  float air_quality;
  int air_quality_accuracy;
  float static_air_quality;
  int static_air_quality_accuracy;
  float eco2;
  int eco2_accuracy;
  float voc;
  int voc_accuracy;
} bsec_outputs_t;

bsec_outputs_t lastOutput;


class Request{
  public:
  Request(String &in){
    // support VERB PATH and HTTP VERSION to start...
    /* GET / HTTP/1.1
     *  <header0>
     *  ...
     *  <headerN>
     *  
     *  <body>
     */
    //if(hasSerial) Serial.println("Parsing\n******"+ in +"\n******");
    verb = Verb::Invalid;
    path = "/";
    httpVersionMajor = 1;
    httpVersionMinor = 0;
    int verbEnd = in.indexOf(" ");
    //if(hasSerial) Serial.print("verbEnd: ");
    //if(hasSerial) Serial.println(verbEnd);
    if(verbEnd > 8 || verbEnd < 1){
      if(hasSerial) Serial.print("VerbEnd >8 or <1: ");
      if(hasSerial) Serial.println(verbEnd);
      return;
    }
    String sverb = in.substring(0, verbEnd);
    sverb.toLowerCase();
    //if(hasSerial) Serial.println("sverb: " + sverb);
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
    //if(hasSerial) Serial.println("rest: ******" + rest + "******");
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
    //if(hasSerial) Serial.print("pathEnd: ");
    //if(hasSerial) Serial.println(pathEnd);
    if(pathEnd == -1){
      if(hasSerial) Serial.print("PathEnd == -1! ");
      if(hasSerial) Serial.println(rest.indexOf(" "));
      return;
    }
    path = rest.substring(0, pathEnd);
    //if(hasSerial) Serial.print("path parsed: ");
    //if(hasSerial) Serial.println(path);
    rest = rest.substring(pathEnd+1);
    //if(hasSerial) Serial.println("rest: ******" + rest + "******");
    String versionstring = rest.substring(0, 8);
    //if(hasSerial) Serial.println("version chunk: " + versionstring);
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
    //headers = rest.substring(0);
    headerCount = 0;
    if(hasSerial) Serial.println("Parsing headers...");
    while(tmp.indexOf('\n') && headerCount < 20 && tmp.length()){
      headers_arr[headerCount] = new String;
      headers_arr[headerCount]->concat(tmp.substring(0,tmp.indexOf('\n')));
      //if(hasSerial) Serial.println(headers_arr[headerCount]->c_str());
      tmp = tmp.substring(tmp.indexOf('\n')+1);
      //if(hasSerial) Serial.println("Remaining request length: " + String(tmp.length()));
      headerCount++;
    }
    //if(hasSerial) Serial.println("Header count: " + String(headerCount));
    if(tmp.length() > 1){
      if(hasSerial) Serial.println("Warning, unparsed headers ignored:\n" + tmp);
    }
    tmp = rest.substring(headerEnd);
    if(hasBody) {
      body.concat(rest.substring(headerEnd+1));
    }
    if(hasSerial) Serial.println("\tHeaders length: "+String(headerCount));
    if(hasSerial) Serial.println("\tBody length: "+String(body.length()));
    //if(hasSerial) Serial.println(path);
    //if(hasSerial) Serial.print("\tVersion: ");
    //if(hasSerial) Serial.print(httpVersionMajor);
    //if(hasSerial) Serial.print(".");
    //if(hasSerial) Serial.println(httpVersionMinor);
  }
  ~Request() {
    for(int i=0;i<headerCount;i++){
      delete headers_arr[i];
    }
    //delete[] headers_arr;
  }

  void setBody(String &in){
    body.concat(in);
  }

  Verb getVerb(){
    return verb;
  }
  String getPath(){
    return path;
  }
  private:
  Verb verb;
  String path;
  int httpVersionMajor;
  int httpVersionMinor;
  String* headers_arr[20];
  int headerCount;
  String body;
  
};


String td_s(String in){
  return "<td>"+in+"</td>";
}
String ftd(float in){
  return "<td>"+String(in)+"</td>";
}
String itd(int in){
  return "<td>"+String(in)+"</td>";
}
String ltd(long in){
  return "<td>"+String(in)+"</td>";
}

/* Entry point for the example */
void setup(void)
{
  /* Desired subscription list of BSEC2 outputs */
    bsecSensor sensorList[] = {
            BSEC_OUTPUT_RAW_TEMPERATURE,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_HUMIDITY,
            BSEC_OUTPUT_RAW_GAS,
            BSEC_OUTPUT_RAW_GAS_INDEX,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
            BSEC_OUTPUT_RUN_IN_STATUS,
            BSEC_OUTPUT_STABILIZATION_STATUS,
            BSEC_OUTPUT_GAS_ESTIMATE_1,
            BSEC_OUTPUT_GAS_ESTIMATE_2,
            BSEC_OUTPUT_GAS_ESTIMATE_3,
            BSEC_OUTPUT_GAS_ESTIMATE_4,
            BSEC_OUTPUT_IAQ,
            BSEC_OUTPUT_STATIC_IAQ,
            BSEC_OUTPUT_CO2_EQUIVALENT,
            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT
    };

    Serial.begin(115200);
  #ifdef USE_EEPROM
    EEPROM.begin(BSEC_MAX_STATE_BLOB_SIZE + 1);
  #endif
    Wire.begin();
    pinMode(PANIC_LED, OUTPUT);


  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    //if(hasSerial) Serial.println(F("SSD1306 allocation successful"));
    hasDisplay = true;
    display.display();
    display.clearDisplay();
    delay(1000);
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    display.println(F("Connecting to SSID:"));
    display.println(ssid);
    display.display();
  }

    /* Valid for boards with USB-COM. Wait for 10 seconds until the port is open */
    while (!Serial && millis() < 10000) delay(10);

    if(Serial.availableForWrite()){
      hasSerial = true;
    }
   
    /* Initialize the library and interfaces */
    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire))
    {
        checkBsecStatus(envSensor);
    }

    /* Load the configuration string that stores information on how to classify the detected gas */
    if (!envSensor.setConfig(FieldAir_HandSanitizer_config))
    {
        checkBsecStatus (envSensor);
    }

    envSensor.setTemperatureOffset(temperatureOffset);

    /* Copy state from the EEPROM to the algorithm */
    if (!loadState(envSensor))
    {
        checkBsecStatus (envSensor);
    }

    /* Subscribe for the desired BSEC2 outputs */
    if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_SCAN))
    {
        checkBsecStatus (envSensor);
    }

    /* Whenever new data is available call the newDataCallback function */
    envSensor.attachCallback(newDataCallback);

    if(hasSerial) Serial.println("\nBSEC library version " + \
            String(envSensor.version.major) + "." \
            + String(envSensor.version.minor) + "." \
            + String(envSensor.version.major_bugfix) + "." \
            + String(envSensor.version.minor_bugfix));

  connectToWifi();

  if(hasSerial) Serial.println("");
  if(hasSerial) Serial.println("Connected to WiFi");
  if(hasDisplay){
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    if(wifiApMode){
      display.print(F("AP SSID: "));
      display.println(apSSID);
    } else {
      display.print(F("SSID: "));
      display.println(ssid);
      display.print(F("RSSI: "));
      display.println(WiFi.RSSI());
    }
    display.display();
    delay(2000);
    printWifiStatus();
  }

  server.begin();

#ifdef TLS
  if(hasSerial) Serial.println("Setting up TLS server");
  // and now handle TLS
#if defined(TLS_ECC)
  tlsserver.setECCert(new BearSSL::X509List(serverCert), BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN, new BearSSL::PrivateKey(serverKey));
#else
  tlsserver.setRSACert(new BearSSL::X509List(serverCert), new BearSSL::PrivateKey(serverKey));
#endif
  // Cache SSL sessions to accelerate the TLS handshake.
  tlsserver.setCache(&serverCache);
  tlsserver.begin();
#endif

}

#if defined(HOMEBRIDGE)

  bool updateHb(HTTPClient& client, String devId, long val) {
    bool ret = false;
    client.begin("http://"+String(homebridgeHost)+":"+String(webhookPort)+"/?accessoryId="+devId+"&value="+val);
    int statusCode = client.GET();
    if(statusCode > 0){
      // no error
      if(statusCode == HTTP_CODE_OK || statusCode == HTTP_CODE_MOVED_PERMANENTLY) {
        //if(hasSerial) Serial.println("Got response: "+client.getString());
        ret = true;
      }
      else {
        if(hasSerial) Serial.println("Status != 200 or 301: " + String(statusCode));
      }
    } else {
      if(hasSerial) Serial.println("Error making GET: " + client.errorToString(statusCode));
    }
    client.end();
    return ret;
  }

  bool postTemp(HTTPClient& client) {
    return updateHb(client, tempSensorId, lastOutput.comp_temp_c);
  }

  bool postHumidity(HTTPClient& client) {
    return updateHb(client, humiditySensorId, lastOutput.comp_humidity);
  }

  bool postAqi(HTTPClient& client) {
    if(lastOutput.air_quality_accuracy > 1){
      int aqi = lastOutput.air_quality / 50;
      // basic thresholds are 1-50, 50-100, 101-150, 151-200, 200+
      aqi += 1;
      if(aqi>5) aqi=5;
      return updateHb(client, aqiSensorId, aqi);
    }
    return false;
  }

  HTTPClient http;
  int updateInterval = 30 * 1000;
  long lastUpdate = 0;
  void updateHomebridge() {
    long now = millis();
    if(now > lastUpdate + updateInterval){
      lastUpdate = now;
      postTemp(http);
      postHumidity(http);
      postAqi(http);
    }
  }

#else
  // no-op
  void updateHomebridge() { }

#endif

int bsecRetries = 0;
/* Function that is looped forever */
void loop(void)
{
    /* Call the run function often so that the library can
     * check if it is time to read new data from the sensor
     * and process it.
     */
    if (!envSensor.run()) {
      // only fail hard after a few retries
      if(bsecRetries > 30) {
        checkBsecStatus (envSensor);
      }
      bsecRetries++;
      delay(100);
    }
    updateHomebridge();

    WiFiClient client = server.accept();
    processOneRequest(client);
#ifdef TLS
    WiFiClientSecure clients = tlsserver.accept();
    processOneRequest(clients);
#endif
    int status = WiFi.status();
    WiFiMode_t mode = WiFi.getMode();
    if(mode != WiFiMode_t::WIFI_AP && status != WL_CONNECTED){
      // reconnect? switch to AP mode?
      if(hasSerial) Serial.println("WiFi disconnected!");
      connectToWifi();
    }
}

void errLeds(int bsecStatus, int bsecSensorStatus)
{
  if(hasDisplay){
    display.clearDisplay();
    display.setTextSize(1);             // Normal 1:1 pixel scale
    display.setTextColor(SSD1306_WHITE);        // Draw white text
    display.setCursor(0,0);             // Start at top-left corner
    if(bsecStatus < BSEC_OK) { // error code
        display.println("BSEC error code : " + String(bsecStatus));
    }
    if (bsecSensorStatus < BME68X_OK) {
        display.println("BME68X error code : " + String(bsecSensorStatus));
    }
    display.display();
  }
  int num_errs = 0;
  // only flash the error LEDs for 30 seconds
    while(num_errs < 15)
    {
        digitalWrite(PANIC_LED, HIGH);
        delay(ERROR_DUR);
        digitalWrite(PANIC_LED, LOW);
        delay(ERROR_DUR);
        num_errs++;
    }
}

void updateBsecState(Bsec2 bsec)
{
    static uint16_t stateUpdateCounter = 1;
    bool update = false;

    if (stateUpdateCounter * STATE_SAVE_PERIOD < millis())
    {
        /* Update every STATE_SAVE_PERIOD minutes */
        update = true;
        stateUpdateCounter++;
    }

    if (update && !saveState(bsec))
        checkBsecStatus(bsec);
}

String statusSpinner[] = {"/", "/", "-", "-", "\\", "\\", "|", "|" };
int statusNum = 0;
int statusSize = 8;

void spin(){
    display.setCursor(122,0);
    if(statusNum % 2 == 0) {
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    } else {
      display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    }
    display.print(statusSpinner[statusNum++]);
    display.display();
    if(statusNum >= statusSize){
      statusNum = 0;
    }
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
}

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
    if (!outputs.nOutputs)
        return;

    if(hasSerial) Serial.println("BSEC outputs:\n\ttimestamp = " + String((int) (outputs.output[0].time_stamp / INT64_C(1000000))));
    for (uint8_t i = 0; i < outputs.nOutputs; i++)
    {
        const bsecData output  = outputs.output[i];
        switch (output.sensor_id)
        {
          /*
            client.println(ftd(lastOutput.raw_temp));
            client.println(ftd(lastOutput.raw_pressure));
            client.println(ftd(lastOutput.raw_humidity));
            client.println(ftd(lastOutput.raw_gas));
            client.println(ftd(lastOutput.air_quality));
            client.println(itd(lastOutput.air_quality_accuracy));
            client.println(ftd(lastOutput.comp_temp));
            client.println(ftd(lastOutput.comp_humidity));
            client.println(ftd(lastOutput.static_air_quality));
            client.println(ftd(lastOutput.eco2));
            client.println(ftd(lastOutput.voc));
           */
            case BSEC_OUTPUT_RUN_IN_STATUS:
                if(hasSerial) Serial.println("\tRun in status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                if(hasSerial) Serial.println("\tStabilization status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_IAQ:
                if(hasSerial) Serial.println("\tIAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.air_quality = output.signal;
                lastOutput.air_quality_accuracy = output.accuracy;
                break;
            case BSEC_OUTPUT_STATIC_IAQ:
                if(hasSerial) Serial.println("\tStatic IAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.static_air_quality = output.signal;
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                if(hasSerial) Serial.println("\tCO2 equivalent = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.eco2 = output.signal;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                if(hasSerial) Serial.println("\tbreath VOC = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.voc = output.signal;
                break;
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                if(hasSerial) Serial.println("\ttemperature = " + String(output.signal * 9 / 5 + 32) + "°F" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_temp = output.signal * 9 / 5 + 32;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                if(hasSerial) Serial.println("\tpressure = " + String(output.signal * 0.00029529983071445) + "inHg" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_pressure = output.signal * 0.00029529983071445;
                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                if(hasSerial) Serial.println("\thumidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_humidity = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS:
                if(hasSerial) Serial.println("\tgas resistance = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_gas = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS_INDEX:
                if(hasSerial) Serial.println("\tgas index = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                if(int(output.signal) == 9){
                  updateDisplay();
                }
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                if(hasSerial) Serial.println("\tcompensated temperature = " + String(output.signal * 9 / 5 + 32) + "°F" + ", accuracy: " + String(output.accuracy));
                lastOutput.comp_temp = output.signal * 9 / 5 + 32;
                lastOutput.comp_temp_c = output.signal;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                if(hasSerial) Serial.println("\tcompensated humidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.comp_humidity = output.signal;
                break;
            case BSEC_OUTPUT_GAS_ESTIMATE_1:
            case BSEC_OUTPUT_GAS_ESTIMATE_2:
            case BSEC_OUTPUT_GAS_ESTIMATE_3:
            case BSEC_OUTPUT_GAS_ESTIMATE_4:
                if((int)(output.signal * 10000.0f) > 0) /* Ensure that there is a valid value xx.xx% */
                {
                    if(hasSerial) Serial.println("\t" + \
                      gasName[(int) (output.sensor_id - BSEC_OUTPUT_GAS_ESTIMATE_1)] + \
                      String(" probability : ") + String(output.signal * 100) + "%");
                }
                break;
            default:
                break;
        }
    }
    spin();
    updateBsecState(envSensor);
}

void updateDisplay()
{
  
    // display is 21 chars wide
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    // AQI and CI are 15-16 chars wide
    display.print(F("AQ "));
    int aqi = round(lastOutput.air_quality);
    if(aqi < 51) {        //   0-50  == GOOD
      display.print("Good (");
    } else if(aqi < 101){ // 51-100  == Moderate
      display.print("Mod  (");
    } else if(aqi < 151){ // 101-150 == Unhealthy for sensitive groups
      display.print("USG  (");
    } else if(aqi < 201){ // 151-200 == Unhealthy
      display.print("U/H  (");
    } else if(aqi < 301){ // 201-300 == Very unhealthy
      display.print("VU/H (");
    } else {              // 301+    == Hazardous
      display.print("HAZ! (");
    }
    display.print(aqi);
    //display.print(lastOutput.air_quality);
    display.print(F(") \t A:"));
    int aqa = lastOutput.air_quality_accuracy;
    display.println(aqa);
    /*
    if(aqa == 3){
      display.println(F("3"));
    } else if(aqa == 2){
      display.println(F("2"));
    } else if(aqa == 1) {
      display.println(F("1"));
    } else {
      display.println(F("0"));
    }*/
    // Temp, humidity are 17-18 wide
    display.print(int(round(lastOutput.comp_temp)));
    display.print(F("F "));
    display.print(int(round(lastOutput.comp_humidity)));
    display.print(F("% "));
    display.print(lastOutput.raw_pressure);
    display.println(F("inHg"));
    display.print(F("CO2 "));
    display.print(int(lastOutput.eco2));
    display.print(F(" \t VOC "));
    display.println(lastOutput.voc);
    WiFiMode_t mode = WiFi.getMode();
    if(mode == WiFiMode_t::WIFI_AP){
      display.print(F("AP SSID: "));
      display.print(WiFi.SSID());
    } else {
      int status = WiFi.status();
      if(status != WL_CONNECTED){
        display.print(F("WiFi D/C"));
      } else {
        display.print(F("WiFi signal: ")); // 13 chars
        long rs = WiFi.RSSI();
        rs *= -1;
        if(rs == 0) {
          display.print(F("  D/C"));
        } else if(rs < 70){
          display.print(F(" GOOD"));
        } else if(rs < 80){
          display.print(F(" OKAY"));
        } else if(rs < 90){
          display.print(F(" POOR"));
        } else {
          display.print(F("AWFUL"));
        }
      }
    }
    display.display();
}

void checkBsecStatus(Bsec2 bsec)
{
    if (bsec.status < BSEC_OK)
    {
        if(hasSerial) Serial.println("BSEC error code : " + String(bsec.status));
        errLeds(bsec.status, 0); /* Halt in case of failure */
    } else if (bsec.status > BSEC_OK)
    {
        if(hasSerial) Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK)
    {
        if(hasSerial) Serial.println("BME68X error code : " + String(bsec.sensor.status));
        errLeds(0, bsec.sensor.status); /* Halt in case of failure */
    } else if (bsec.sensor.status > BME68X_OK)
    {
        if(hasSerial) Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

bool loadState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    

    if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
    {
        /* Existing state in EEPROM */
        if(hasSerial) Serial.println("Reading state from EEPROM");
        if(hasSerial) Serial.print("State file: ");
        for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
        {
            bsecState[i] = EEPROM.read(i + 1);
            if(hasSerial) Serial.print(String(bsecState[i], HEX) + ", ");
        }
        if(hasSerial) Serial.println();

        if (!bsec.setState(bsecState))
            return false;
    } else
    {
        /* Erase the EEPROM with zeroes */
        if(hasSerial) Serial.println("Erasing EEPROM");

        for (uint8_t i = 0; i <= BSEC_MAX_STATE_BLOB_SIZE; i++)
            EEPROM.write(i, 0);

        EEPROM.commit();
    }
#endif
    return true;
}

bool saveState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    if (!bsec.getState(bsecState))
        return false;

    if(hasSerial) Serial.println("Writing state to EEPROM");
    if(hasSerial) Serial.print("State file: ");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        EEPROM.write(i + 1, bsecState[i]);
        if(hasSerial) Serial.print(String(bsecState[i], HEX) + ", ");
    }
    if(hasSerial) Serial.println();

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
#endif
    return true;
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  if(hasSerial) Serial.print("SSID: ");
  if(hasSerial) Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  if(hasSerial) Serial.print("IP Address: ");
  if(hasSerial) Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  if(hasSerial) Serial.print("signal strength (RSSI):");
  if(hasSerial) Serial.print(rssi);
  if(hasSerial) Serial.println(" dBm");
}

bool processOneRequest(WiFiClient &client){
  bool resetBoard = false;
  bool reconnectWifi = false;
  String resp = "<table><tr><th>Timestamp [ms]</th><th>raw temperature [°F]</th><th>pressure [mmHg]</th><th>raw relative humidity [%]</th><th>gas [Ohm]</th><th>IAQ</th><th>IAQ accuracy</th><th>temperature [°F]</th><th>relative humidity [%]</th><th>Static IAQ</th><th>CO2 equivalent</th><th>breath VOC equivalent</tr>\n";
  if (client) {
    if(hasSerial) Serial.println("New client");
    bool currentLineIsBlank = true;
    String input = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        input += c;
        if (c == '\n' && currentLineIsBlank) {
          Request req = Request(input);
          //Serial.write(input.c_str());
          if(req.getPath() == "/"){
            if(hasSerial) Serial.println("Root req, responding!");
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html; charset=utf-8");
            client.println("Connection: close");
            client.println();
            client.println("<!DOCTYPE HTML>");
            client.println("<html><head></head><body>");
            unsigned long time_trigger = millis();
            client.println(resp);
            client.println("<tr>");
            client.println(ltd(time_trigger));
            client.println(ftd(lastOutput.raw_temp));
            client.println(ftd(lastOutput.raw_pressure));
            client.println(ftd(lastOutput.raw_humidity));
            client.println(ftd(lastOutput.raw_gas));
            client.println(ftd(lastOutput.air_quality));
            client.println(itd(lastOutput.air_quality_accuracy));
            client.println(ftd(lastOutput.comp_temp));
            client.println(ftd(lastOutput.comp_humidity));
            client.println(ftd(lastOutput.static_air_quality));
            client.println(ftd(lastOutput.eco2));
            client.println(ftd(lastOutput.voc));
            client.println("</tr></table></body></html>");
          } else if(req.getPath() == "/state"){
            if(hasSerial) Serial.println("State req, responding!");
            if(req.getVerb() == Verb::Get){
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html; charset=utf-8");
              client.println("Connection: close");
              client.println();
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body><pre>");
              if (!envSensor.getState(bsecState)){
                  client.println("ERROR</pre></body></html>");
                  break;
              } else {
                if(hasSerial) Serial.println("Writing state to req");
                for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
                {
                    client.print(String(bsecState[i], HEX) + ", ");
                }
                client.println("</pre></body></html>");
                break;
              }
            } else if(req.getVerb() == Verb::Put){
              if(hasSerial) Serial.println("Saving state based on on-demand Put");
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html; charset=utf-8");
              client.println("Connection: close");
              client.println();
              
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body>");
              if(!saveState(envSensor)){
                client.println("<p>Could not save state to (Flash-emulated) EEPROM</p><pre>");
                for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
                {
                    client.print(String(bsecState[i], HEX) + ", ");
                }
                client.println("</pre></body></html>");
                break;
              } else {
                client.println("State saved to (Flash-emulated) EEPROM</body></html>");
                break;
              }
            } else if(req.getVerb() == Verb::Post){
              if(hasSerial) Serial.println("Updating state from post body");
            }
          } else if(req.getPath() == "/reset") {
            if(req.getVerb() == Verb::Post){
              client.println("HTTP/1.1 303 See Other");
              client.println("Location: /");
            } else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html; charset=utf-8");
            }
            client.println("Connection: close");
            client.println();
            client.println("<!DOCTYPE HTML>");
            if(req.getVerb() == Verb::Post){
              client.println("<html><head></head><body>Resetting...<br>click <a href='/'>here</a> if you are not automatically redirected.</head></body></html>");
              if(hasSerial) Serial.println("Reset POST request, the board is resetting NOW!");
              resetBoard = true;
            } else {
              client.println("<html><head></head><body><p>Click the button below to reset the board</p><form action='/reset' method='post'><button type=submit>RESET BOARD</button></form></head></body></html>");
            }
          } else if(req.getPath() == "/reconnect") {
            if(req.getVerb() == Verb::Post){
              client.println("HTTP/1.1 303 See Other");
              client.println("Location: /");
              client.println("");
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body>Resetting...<br>click <a href='/'>here</a> if you are not automatically redirected.</head></body></html>");
              if(hasSerial) Serial.println("Reset POST request, the board is resetting NOW!");
              // reconnect to wifi
              
              reconnectWifi = true;
            } else {
              client.println("HTTP/1.1 200 OK");
              client.println("Content-Type: text/html; charset=utf-8");
              client.println("");
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body><p>Click the button below to reset the Wifi connection</p><form action='/reconnect' method='post'><button type=submit>Reconnect Wifi</button></form></head></body></html>");
              
            }
          }

          break;
        }
        if (c == '\n') {
          currentLineIsBlank = true;
        } else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    }
    delay(1);
    client.stop();
    if(hasSerial) Serial.println("Client disconnected");
    if(resetBoard){
      rp2040.reboot();
    }
    if(reconnectWifi){
      connectToWifi();
    }
    return true;
  }
  return false;
}
