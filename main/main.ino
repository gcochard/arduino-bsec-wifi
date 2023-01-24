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
bool displayEnabled = false;
/* end oled screen stuff */




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

/* Helper functions declarations */
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
bool processOneRequest(void);

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

enum class Verb { Get, Put, Post, Delete, Head, Invalid };

typedef struct
{
  float raw_temp;
  float raw_pressure;
  float raw_humidity;
  float raw_gas;
  float raw_gas_index;
  float comp_temp;
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
    //Serial.println("Parsing\n******"+ in +"\n******");
    verb = Verb::Invalid;
    path = "/";
    httpVersionMajor = 1;
    httpVersionMinor = 0;
    int verbEnd = in.indexOf(" ");
    //Serial.print("verbEnd: ");
    //Serial.println(verbEnd);
    if(verbEnd > 8 || verbEnd < 1){
      Serial.print("VerbEnd >8 or <1: ");
      Serial.println(verbEnd);
      return;
    }
    String sverb = in.substring(0, verbEnd);
    sverb.toLowerCase();
    //Serial.println("sverb: " + sverb);
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
    //Serial.println("rest: ******" + rest + "******");
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
    //Serial.print("pathEnd: ");
    //Serial.println(pathEnd);
    if(pathEnd == -1){
      Serial.print("PathEnd == -1! ");
      Serial.println(rest.indexOf(" "));
      return;
    }
    path = rest.substring(0, pathEnd);
    //Serial.print("path parsed: ");
    //Serial.println(path);
    rest = rest.substring(pathEnd+1);
    //Serial.println("rest: ******" + rest + "******");
    String versionstring = rest.substring(0, 8);
    //Serial.println("version chunk: " + versionstring);
    if(!versionstring.startsWith("HTTP")){
      Serial.print("versionString not HTTP/X.Y: '");
      Serial.print(versionstring);
      Serial.println("'");
      return;
    }
    int majorStart = versionstring.indexOf("/")+1;
    httpVersionMajor = versionstring.substring(majorStart,majorStart+1).toInt();
    httpVersionMinor = versionstring.substring(majorStart+2,majorStart+3).toInt();
    Serial.println("HTTP request parsed. Verb: " + verbStr + "\n\tPath: " + String(path) + "\n\tVersion: " + String(httpVersionMajor) + "." + String(httpVersionMinor));
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
    Serial.println("Parsing headers...");
    while(tmp.indexOf('\n') && headerCount < 20 && tmp.length()){
      headers_arr[headerCount] = new String;
      headers_arr[headerCount]->concat(tmp.substring(0,tmp.indexOf('\n')));
      //Serial.println(headers_arr[headerCount]->c_str());
      tmp = tmp.substring(tmp.indexOf('\n')+1);
      //Serial.println("Remaining request length: " + String(tmp.length()));
      headerCount++;
    }
    //Serial.println("Header count: " + String(headerCount));
    if(tmp.length() > 1){
      Serial.println("Warning, unparsed headers ignored:\n" + tmp);
    }
    tmp = rest.substring(headerEnd);
    if(hasBody) {
      body.concat(rest.substring(headerEnd+1));
    }
    Serial.println("\tHeaders length: "+String(headerCount));
    Serial.println("\tBody length: "+String(body.length()));
    //Serial.println(path);
    //Serial.print("\tVersion: ");
    //Serial.print(httpVersionMajor);
    //Serial.print(".");
    //Serial.println(httpVersionMinor);
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

    /* Valid for boards with USB-COM. Wait until the port is open */
    //while (!Serial) delay(10);
   
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

    Serial.println("\nBSEC library version " + \
            String(envSensor.version.major) + "." \
            + String(envSensor.version.minor) + "." \
            + String(envSensor.version.major_bugfix) + "." \
            + String(envSensor.version.minor_bugfix));


  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation successful"));
    displayEnabled = true;
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

  // attempt to connect to Wifi network:
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);

  const unsigned long connectionTimeout = 60L * 1000L;
  unsigned long startTime = millis();
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
      if(displayEnabled){
        display.print(".");
        display.display();
      }
      if(millis() - startTime > connectionTimeout){
        //wifiApMode = true;
        WiFi.beginAP(apSSID);
        break;
      }
  }

  Serial.println("");
  Serial.println("Connected to WiFi");
  if(displayEnabled){
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    display.print(F("RSSI: "));
    display.println(WiFi.RSSI());
    display.print(F("SSID: "));
    display.println(ssid);
    display.display();
    delay(2000);
    printWifiStatus();
  }

  server.begin();
}

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
    processOneRequest();
    //if(WiFi.
}

void errLeds(int bsecStatus, int bsecSensorStatus)
{
  if(displayEnabled){
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

void newDataCallback(const bme68xData data, const bsecOutputs outputs, Bsec2 bsec)
{
    if (!outputs.nOutputs)
        return;

    Serial.println("BSEC outputs:\n\ttimestamp = " + String((int) (outputs.output[0].time_stamp / INT64_C(1000000))));
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
                Serial.println("\tRun in status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                Serial.println("\tStabilization status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_IAQ:
                Serial.println("\tIAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.air_quality = output.signal;
                lastOutput.air_quality_accuracy = output.accuracy;
                break;
            case BSEC_OUTPUT_STATIC_IAQ:
                Serial.println("\tStatic IAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.static_air_quality = output.signal;
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                Serial.println("\tCO2 equivalent = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.eco2 = output.signal;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                Serial.println("\tbreath VOC = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.voc = output.signal;
                break;
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                Serial.println("\ttemperature = " + String(output.signal * 9 / 5 + 32) + "°F" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_temp = output.signal * 9 / 5 + 32;
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                Serial.println("\tpressure = " + String(output.signal * 0.00029529983071445) + "inHg" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_pressure = output.signal * 0.00029529983071445;
                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                Serial.println("\thumidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_humidity = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS:
                Serial.println("\tgas resistance = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_gas = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS_INDEX:
                Serial.println("\tgas index = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                if(int(output.signal) == 9){
                  updateDisplay();
                }
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                Serial.println("\tcompensated temperature = " + String(output.signal * 9 / 5 + 32) + "°F" + ", accuracy: " + String(output.accuracy));
                lastOutput.comp_temp = output.signal * 9 / 5 + 32;
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                Serial.println("\tcompensated humidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.comp_humidity = output.signal;
                break;
            case BSEC_OUTPUT_GAS_ESTIMATE_1:
            case BSEC_OUTPUT_GAS_ESTIMATE_2:
            case BSEC_OUTPUT_GAS_ESTIMATE_3:
            case BSEC_OUTPUT_GAS_ESTIMATE_4:
                if((int)(output.signal * 10000.0f) > 0) /* Ensure that there is a valid value xx.xx% */
                {
                    Serial.println("\t" + \
                      gasName[(int) (output.sensor_id - BSEC_OUTPUT_GAS_ESTIMATE_1)] + \
                      String(" probability : ") + String(output.signal * 100) + "%");
                }
                break;
            default:
                break;
        }
    }

    updateBsecState(envSensor);
}

void updateDisplay()
{
  
    // display is 21 chars wide
    display.clearDisplay();
    display.setCursor(0,0);             // Start at top-left corner
    // AQI and CI are 15-16 chars wide
    display.print(F("AQ "));
    int aqi = int(lastOutput.air_quality);
    if(aqi < 51) {        //   0-50  == GOOD
      display.print("Good");
    } else if(aqi < 101){ // 51-100  == Moderate
      display.print("Mod ");
    } else if(aqi < 151){ // 101-150 == Unhealthy for sensitive groups
      display.print("USG ");
    } else if(aqi < 201){ // 151-200 == Unhealthy
      display.print("U/H ");
    } else if(aqi < 301){ // 201-300 == Very unhealthy
      display.print("VU/H");
    } else {              // 301+    == Hazardous
      display.print("HAZ!");
    }
    //display.print(lastOutput.air_quality);
    display.print(F("\tAcc "));
    int aqa = lastOutput.air_quality_accuracy;
    if(aqa == 3){
      display.println(F("   High  "));
    } else if(aqa == 2){
      display.println(F("    Med  "));
    } else if(aqa == 1) {
      display.println(F("    Low  "));
    } else {
      display.println(F("Stabilizn"));
    }
    // Temp, humidity are 17-18 wide
    display.print(F("T "));
    display.print(lastOutput.comp_temp);
    display.print(F("*F"));
    display.print(F(" \t H "));
    display.print(lastOutput.comp_humidity);
    display.println(F("%"));
    display.print(F("P "));
    display.print(lastOutput.raw_pressure);
    display.println(F("inHg"));
    WiFiMode_t mode = getMode();
    if(mode == WiFiMode_t::WIFI_AP){
      display.print(F("AP SSID: "));
      display.print(WiFi.SSID());
    } else {
      display.print(F("WIFI signal: ")); // 13 chars
    
      long rs = WiFi.RSSI();
      rs *= -1;
      if(rs < 70){
        display.print(F(" GOOD"));
      } else if(rs < 80){
        display.print(F(" OKAY"));
      } else if(rs < 90){
        display.print(F(" POOR"));
      } else {
        display.print(F("AWFUL"));
      }
    }
    display.display();
}

void checkBsecStatus(Bsec2 bsec)
{
    if (bsec.status < BSEC_OK)
    {
        Serial.println("BSEC error code : " + String(bsec.status));
        errLeds(bsec.status, 0); /* Halt in case of failure */
    } else if (bsec.status > BSEC_OK)
    {
        Serial.println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK)
    {
        Serial.println("BME68X error code : " + String(bsec.sensor.status));
        errLeds(0, bsec.sensor.status); /* Halt in case of failure */
    } else if (bsec.sensor.status > BME68X_OK)
    {
        Serial.println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

bool loadState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    

    if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
    {
        /* Existing state in EEPROM */
        Serial.println("Reading state from EEPROM");
        Serial.print("State file: ");
        for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
        {
            bsecState[i] = EEPROM.read(i + 1);
            Serial.print(String(bsecState[i], HEX) + ", ");
        }
        Serial.println();

        if (!bsec.setState(bsecState))
            return false;
    } else
    {
        /* Erase the EEPROM with zeroes */
        Serial.println("Erasing EEPROM");

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

    Serial.println("Writing state to EEPROM");
    Serial.print("State file: ");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        EEPROM.write(i + 1, bsecState[i]);
        Serial.print(String(bsecState[i], HEX) + ", ");
    }
    Serial.println();

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    EEPROM.commit();
#endif
    return true;
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

bool processOneRequest(){
  bool resetBoard = false;
  String resp = "<table><tr><th>Timestamp [ms]</th><th>raw temperature [°F]</th><th>pressure [mmHg]</th><th>raw relative humidity [%]</th><th>gas [Ohm]</th><th>IAQ</th><th>IAQ accuracy</th><th>temperature [°F]</th><th>relative humidity [%]</th><th>Static IAQ</th><th>CO2 equivalent</th><th>breath VOC equivalent</tr>\n";
  WiFiClient client = server.available();
  if (client) {
    Serial.println("New client");
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
            Serial.println("Root req, responding!");
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
            Serial.println("State req, responding!");
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
                Serial.println("Writing state to req");
                for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
                {
                    client.print(String(bsecState[i], HEX) + ", ");
                }
                client.println("</pre></body></html>");
                break;
              }
            } else if(req.getVerb() == Verb::Put){
              Serial.println("Saving state based on on-demand Put");
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
              Serial.println("Updating state from post body");
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
              Serial.println("Reset POST request, the board is resetting NOW!");
              resetBoard = true;
            } else {
              client.println("<html><head></head><body><p>Click the button below to reset the board</p><form action='/reset' method='post'><button type=submit>RESET BOARD</button></form></head></body></html>");
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
    Serial.println("Client disconnected");
    if(resetBoard){
      rp2040.reboot();
    }
    return true;
  }
  return false;
}
