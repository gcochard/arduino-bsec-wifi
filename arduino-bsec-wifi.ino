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

#include <ArduinoOTA.h>

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
#include "settings.h"

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

void commitLast(void);

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

/**
 * @brief : This function wipes the state stored in EEPROM
 */
bool wipeState(void);

/* Create an object of the class Bsec2 */
Bsec2 envSensor;
#ifdef USE_EEPROM
static uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE];
#endif
/* Gas estimate names will be according to the configuration classes used */
const String gasName[] = { "Field Air", "Hand sanitizer", "Undefined 3", "Undefined 4"};

/**
 * Battery measurement support!
 * Mostly taken directly from https://github.com/raspberrypi/pico-examples/pull/326
 * Seems to work even within an HTTP request handler!
 */
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "pico/cyw43_arch/arch_threadsafe_background.h"  //to get access to void cyw43_thread_enter(void);  and void cyw43_thread_exit(void);
#ifndef CYW43_WL_GPIO2
#define CYW43_WL_GPIO2 2  //this is not defined in pico_w.h
#endif
const float VSYS_CONVERSION_FACTOR = 3 * 3.3f / (1 << 12);
//Set GPIO29 back to settings for wifi usage (shared pin with ADC3, so settings are changed when activating ADC3)
void SetGPIO29WifiStatus(){
    gpio_set_function(29, GPIO_FUNC_PIO1); //7
    gpio_pull_down(29);
    gpio_set_slew_rate(29, GPIO_SLEW_RATE_FAST); //1
    gpio_set_drive_strength(29, GPIO_DRIVE_STRENGTH_12MA); //3
}

float measureVsys(){
  //WL_GPIO2, IP VBUS sense - high if VBUS is present, else low
  uint vbus = cyw43_arch_gpio_get(CYW43_WL_GPIO2);
  if(vbus == 0){
    // low, on battery, measure voltage
    print("on battery!");
  } else {
    print("on USB!");
  }

    // first let's prevent the wifi background thread from processing...
    cyw43_thread_enter();

    //initialize and select ADC3/GPIO29
    adc_gpio_init(29);
    adc_select_input(3);

    //Read ADC3, result is 1/3 of VSYS, so we still need to multiply the conversion factor with 3 to get the input voltage
    uint16_t adc3 = adc_read();
    float vsys = adc3 * VSYS_CONVERSION_FACTOR;

    // return gp29 to its previous state
    SetGPIO29WifiStatus();
    cyw43_thread_exit();
    return vsys;
}
/**
 * End battery measurement code
 */

WiFiServer server(80);

#ifdef TLS
#include <WiFiServerSecure.h>
WiFiServerSecure tlsserver(443);
ServerSessions serverCache(5);
#endif

String currentSsid = ssid;
String currentPass = pass;
String metricLocation = defaultMetricLocation;

void print(String val){
  if(hasSerial){
    Serial.print(val);
  }
}

void println(String val){
  print(val+"\n");
}

void connectToWifi(){
  // attempt to connect to Wifi network:
  println("Attempting to connect to SSID: "+currentSsid+" with password: "+currentPass);

  unsigned long startTime = millis();
  WiFi.begin(currentSsid.c_str(), currentPass.c_str());
  wifiApMode = false;
  bool wifiFallback = false;
  while (WiFi.status() != WL_CONNECTED) {
      unsigned int stat = WiFi.status();
      spin();
      delay(500);
      print(".");
      print(String(WiFi.status()));
      if(hasDisplay){
        display.print(".");
        display.display();
      }
      if(millis() - startTime > connectionTimeout && wifiFallback){
        wifiApMode = true;
        currentSsid = apSSID;
        WiFi.beginAP(currentSsid.c_str());
        break;
      } else if(millis() - startTime > connectionTimeout && stat == WL_CONNECT_FAILED){
        wifiFallback = true;
        println(String("\nFalling back to header-defined defaults, SSID: ")+ssid+" and password: "+pass);
        WiFi.begin(ssid, pass);
      }
  }
  // set the RTC using NTP
  NTP.begin("pool.ntp.org", "time.nist.gov");
  NTP.waitSet();
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  print("Current time: ");
  print(asctime(&timeinfo));
  char buf[20];
  ltoa(now, buf, 10);
  println(String(" (") + buf + ")");
}

typedef struct
{
  unsigned long timestamp;
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
bsec_outputs_t committedOutput;

#include "request.h"

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

Settings settings(hasSerial);
/* Entry point for the example */
void setup(void)
{

  // power the bme board from GP22 to avoid 3v3 pin conflict
  pinMode(22, OUTPUT);
  digitalWrite(22, HIGH);
  
  /* Desired subscription list of BSEC2 outputs */
    bsecSensor sensorList[] = {
            BSEC_OUTPUT_RAW_TEMPERATURE,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_HUMIDITY,
            BSEC_OUTPUT_RAW_GAS,
/*
            BSEC_OUTPUT_RAW_GAS_INDEX,
*/
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
            BSEC_OUTPUT_RUN_IN_STATUS,
            BSEC_OUTPUT_STABILIZATION_STATUS,
/*
            BSEC_OUTPUT_GAS_ESTIMATE_1,
            BSEC_OUTPUT_GAS_ESTIMATE_2,
            BSEC_OUTPUT_GAS_ESTIMATE_3,
            BSEC_OUTPUT_GAS_ESTIMATE_4,
*/
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
    //println(F("SSD1306 allocation successful"));
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
      settings.setHasSerial(true);
    }

    
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if(hasSerial) Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if(hasSerial) Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      println("End Failed");
    }
  });
  
   
    /* Initialize the library and interfaces */
    if (!envSensor.begin(BME68X_I2C_ADDR_HIGH, Wire))
    {
      if(!envSensor.begin(BME68X_I2C_ADDR_LOW, Wire)) {
        checkBsecStatus(envSensor);
      }
    }

    /* Load the configuration string that stores information on how to classify the detected gas */
/*
    if (!envSensor.setConfig(FieldAir_HandSanitizer_config))
    {
        checkBsecStatus (envSensor);
    }
*/

    envSensor.setTemperatureOffset(temperatureOffset);

    /* Copy state from the EEPROM to the algorithm */
    if (!loadState(envSensor))
    {
        checkBsecStatus (envSensor);
    }

    /* Subscribe for the desired BSEC2 outputs */
    if (!envSensor.updateSubscription(sensorList, ARRAY_LEN(sensorList), BSEC_SAMPLE_RATE_LP))
    {
        checkBsecStatus (envSensor);
    }

    /* Whenever new data is available call the newDataCallback function */
    envSensor.attachCallback(newDataCallback);

    println("\nBSEC library version " + \
            String(envSensor.version.major) + "." \
            + String(envSensor.version.minor) + "." \
            + String(envSensor.version.major_bugfix) + "." \
            + String(envSensor.version.minor_bugfix));
  settings.getWifiSettings(currentSsid, currentPass);
  println("Current SSID: "+currentSsid+"\ncurrentPass: "+currentPass);
  settings.getLocation(metricLocation);
  connectToWifi();

  println("");
  println("Connected to WiFi");
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
  ArduinoOTA.begin();

  server.begin();

#ifdef TLS
  println("Setting up TLS server");
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
    String uri = "http://"+homebridgeHost+":"+webhookPort+"/?accessoryId="+devId+"&value="+val;
    client.begin(uri);
    int statusCode = client.GET();
    if(statusCode > 0){
      // no error
      if(statusCode == HTTP_CODE_OK || statusCode == HTTP_CODE_MOVED_PERMANENTLY) {
        //println("Got response: "+client.getString());
        ret = true;
      }
      else {
        println("Status != 200 or 301: " + String(statusCode)+"\n\t"+uri);
      }
    } else {
      println("Error making GET: " + client.errorToString(statusCode) + "\n\t" + uri);
    }
    client.end();
    return ret;
  }

  bool postTemp(HTTPClient& client) {
    return updateHb(client, String(metricLocation) + tempSensorId, committedOutput.comp_temp_c);
  }

  bool postHumidity(HTTPClient& client) {
    return updateHb(client, String(metricLocation) + humiditySensorId, committedOutput.comp_humidity);
  }

  bool postAqi(HTTPClient& client) {
    if(committedOutput.air_quality_accuracy > 1){
      int aqi = lastOutput.air_quality / 50;
      // basic thresholds are 1-50, 50-100, 101-150, 151-200, 200+
      aqi += 1;
      if(aqi>5) aqi=5;
      return updateHb(client, String(metricLocation) + aqiSensorId, aqi);
    }
    return false;
  }

  HTTPClient http;
  int updateInterval = 30 * 1000;
  long lastUpdate = 0;
  void updateHomebridge() {
    long now = millis();
    if(now > lastUpdate + updateInterval && committedOutput.raw_temp != 0){
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
  ArduinoOTA.handle();

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
      println("WiFi disconnected!");
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

    println("BSEC outputs:\n\ttimestamp = " + String((int) (outputs.output[0].time_stamp / INT64_C(1000000))));
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
                println("\tRun in status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_STABILIZATION_STATUS:
                println("\tStabilization status = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_IAQ:
                println("\tIAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.air_quality = output.signal;
                lastOutput.air_quality_accuracy = output.accuracy;
                break;
            case BSEC_OUTPUT_STATIC_IAQ:
                println("\tStatic IAQ = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.static_air_quality = output.signal;
                break;
            case BSEC_OUTPUT_CO2_EQUIVALENT:
                println("\tCO2 equivalent = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.eco2 = output.signal;
                break;
            case BSEC_OUTPUT_BREATH_VOC_EQUIVALENT:
                println("\tbreath VOC = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.voc = output.signal;
                break;
            case BSEC_OUTPUT_RAW_TEMPERATURE:
                lastOutput.raw_temp = output.signal * 9 / 5 + 32;
                println("\ttemperature = " + String(lastOutput.raw_temp) + "°F" + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_RAW_PRESSURE:
                lastOutput.raw_pressure = output.signal * 0.00029529983071445 + pressureOffset;
                println("\tpressure = " + String(lastOutput.raw_pressure) + "inHg" + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_RAW_HUMIDITY:
                println("\thumidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_humidity = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS:
                println("\tgas resistance = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                lastOutput.raw_gas = output.signal;
                break;
            case BSEC_OUTPUT_RAW_GAS_INDEX:
                println("\tgas index = " + String(output.signal) + ", accuracy: " + String(output.accuracy));
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE:
                lastOutput.comp_temp = output.signal * 9 / 5 + 32;
                lastOutput.comp_temp_c = output.signal;
                println("\tcompensated temperature = " + String(lastOutput.comp_temp) + "°F" + ", accuracy: " + String(output.accuracy));
                // commit the measurements here
                updateDisplay();
                commitLast();
                break;
            case BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY:
                println("\tcompensated humidity = " + String(output.signal) + "%" + ", accuracy: " + String(output.accuracy));
                lastOutput.comp_humidity = output.signal;
                break;
            case BSEC_OUTPUT_GAS_ESTIMATE_1:
            case BSEC_OUTPUT_GAS_ESTIMATE_2:
            case BSEC_OUTPUT_GAS_ESTIMATE_3:
            case BSEC_OUTPUT_GAS_ESTIMATE_4:
                if((int)(output.signal * 10000.0f) > 0) /* Ensure that there is a valid value xx.xx% */
                {
                    println("\t" + \
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

void commitLast() {
  memcpy(&committedOutput, &lastOutput, sizeof(bsec_outputs_t));
  committedOutput.timestamp = time(nullptr);
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
        println("BSEC error code : " + String(bsec.status));
        errLeds(bsec.status, 0); /* Halt in case of failure */
    } else if (bsec.status > BSEC_OK)
    {
        println("BSEC warning code : " + String(bsec.status));
    }

    if (bsec.sensor.status < BME68X_OK)
    {
        println("BME68X error code : " + String(bsec.sensor.status));
        errLeds(0, bsec.sensor.status); /* Halt in case of failure */
    } else if (bsec.sensor.status > BME68X_OK)
    {
        println("BME68X warning code : " + String(bsec.sensor.status));
    }
}

bool loadState(Bsec2 bsec)
{
#ifdef USE_EEPROM
    

    if (EEPROM.read(0) == BSEC_MAX_STATE_BLOB_SIZE)
    {
        /* Existing state in EEPROM */
        println("Reading state from EEPROM");
        print("State file: ");
        for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
        {
            bsecState[i] = EEPROM.read(i + 1);
            print(String(bsecState[i], HEX) + ", ");
        }
        println("");

        if (!bsec.setState(bsecState))
            return false;
    } else
    {
        /* Erase the EEPROM with zeroes */
        println("Erasing EEPROM");

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

    println("Writing state to EEPROM");
    print("State file: ");

    for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        EEPROM.write(i + 1, bsecState[i]);
        print(String(bsecState[i], HEX) + ", ");
    }
    println("");

    EEPROM.write(0, BSEC_MAX_STATE_BLOB_SIZE);
    return EEPROM.commit();
#endif
    return true;
}

bool wipeState()
{
#ifdef USE_EEPROM
    /* Erase the EEPROM with zeroes */
    println("Erasing EEPROM");

    for (uint8_t i = 0; i <= BSEC_MAX_STATE_BLOB_SIZE; i++)
    {
        EEPROM.write(i, 0);
    }

    return EEPROM.commit();
#endif
    return true;
}

String getWifiStatus() {
  return "Current SSID: " + WiFi.SSID() + "\nIP Address: " + WiFi.localIP().toString() + "\nSignal Strength (RSSI): " + String(WiFi.RSSI()) + " dBm\n\n" + 
  "Default SSID: \"" + ssid + "\"\nFilesystem SSID: \"" + currentSsid + "\"\n\nDefault pass: \"" + pass + "\"\nFilesystem pass: \"" + currentPass + "\"";
}

void printWifiStatus() {
  // print the SSID of the network you're attached to:
  print("SSID: ");
  println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  print("IP Address: ");
  println(ip.toString());

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  print("signal strength (RSSI):");
  print(String(rssi));
  println(" dBm");
}

void printHead(WiFiClient &client, int statusCode, String contentType) {
    client.println("HTTP/1.1 "+String(statusCode)+" OK");
    client.println("Content-Type: "+contentType+"; charset=utf-8");
    client.println("Connection: close");
    client.println();
}

String metric(String key, int val, String location, String timestamp) {
  return key + "{group=\"environment\", location=\""+location+"\"} " + val + " " + timestamp +"000\n";
}

String metric(String key, float val, String location, String timestamp) {
  return key + "{group=\"environment\", location=\""+location+"\"} " + val + " " + timestamp + "000\n";
}

bool processOneRequest(WiFiClient &client){
  bool resetBoard = false;
  bool reconnectWifi = false;
  String resp = "<table><tr><th>Timestamp [ms]</th><th>raw temperature [°F]</th><th>pressure [mmHg]</th><th>raw relative humidity [%]</th><th>gas [Ohm]</th><th>IAQ</th><th>IAQ accuracy</th><th>temperature [°F]</th><th>relative humidity [%]</th><th>Static IAQ</th><th>CO2 equivalent</th><th>breath VOC equivalent</tr>\n";
  if (client) {
    println("New client");
    bool currentLineIsBlank = true;
    String input = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        input += c;
        if (c == '\n' && currentLineIsBlank) {
          Request req = Request(input, hasSerial);
          if(req.getVerb() == Verb::Post){
            String body = "";
            char c;
            while(client.available()){
              // read the POST body
              c = client.read();
              body += c;
            }
            req.setBody(body);
          }
          //Serial.write(input.c_str());
          if(req.getPath() == "/"){
            println("Root req, responding!");
            printHead(client, 200, "text/html");
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
            println("State req, responding!");
            if(req.getVerb() == Verb::Get){
              printHead(client, 200, "text/html");
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body><pre>");
              if (!envSensor.getState(bsecState)){
                  client.println("ERROR</pre></body></html>");
                  break;
              } else {
                println("Writing state to req");
                for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++)
                {
                    client.print(String(bsecState[i], HEX) + ", ");
                }
                client.println("</pre></body></html>");
                break;
              }
            } else if(req.getVerb() == Verb::Put){
              println("Saving state based on on-demand Put");
              printHead(client, 200, "text/html");
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
              println("Updating state from post body not yet implemented");
              client.println(req.getBody());
            } else if(req.getVerb() == Verb::Delete){
              println("Wiping state!");
              bool wiped = wipeState();
              printHead(client, 200, "text/html");
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body>");
              if(wiped) {
                client.println("<p>Wiped state!</p>");
              } else {
                client.println("<p>Could not wipe state!</p>");
              }
              client.println("</body></html>");
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
              println("Reset POST request, the board is resetting NOW!");
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
              println("Reconnect POST request, the board is reconnecting to wifi NOW!");
              // reconnect to wifi
              
              reconnectWifi = true;
            } else {
              printHead(client, 200, "text/html");
              client.println("<!DOCTYPE HTML>");
              client.println("<html><head></head><body><p>Click the button below to reset the Wifi connection</p><form action='/reconnect' method='post'><button type=submit>Reconnect Wifi</button></form></head></body></html>");
            }
          } else if(req.getPath() == "/metrics") {
            if(committedOutput.raw_temp == 0){
              printHead(client, 500, "text/plain");
            } else {
            printHead(client, 200, "text/plain");
              char buf[20];
              // use timestamp of the last full read for the sensor metrics
              ltoa(committedOutput.timestamp, buf, 10);
              String ts = String(buf);
              client.print(metric("rawtemp", committedOutput.raw_temp, metricLocation, ts));
              client.print(metric("pressure", committedOutput.raw_pressure, metricLocation, ts));
              client.print(metric("rawhumidity", committedOutput.raw_humidity, metricLocation, ts));
              client.print(metric("rawgas", committedOutput.raw_gas, metricLocation, ts));
              client.print(metric("aqi", committedOutput.air_quality, metricLocation, ts));
              client.print(metric("aqi_accuracy", committedOutput.air_quality_accuracy, metricLocation, ts));
              client.print(metric("temperatureF", committedOutput.comp_temp, metricLocation, ts));
              client.print(metric("humidity", committedOutput.comp_humidity, metricLocation, ts));
              client.print(metric("static_aqi", committedOutput.static_air_quality, metricLocation, ts));
              client.print(metric("eco2", committedOutput.eco2, metricLocation, ts));
              client.print(metric("tvoc", committedOutput.voc, metricLocation, ts));
              // and grab a fresh timestamp for the board metrics as they're measured right now
              ltoa(time(nullptr), buf, 10);
              ts = String(buf);
              int rssi = WiFi.RSSI();
              client.print(metric("heap_free", rp2040.getFreeHeap(), metricLocation, ts));
              client.print(metric("heap_used", rp2040.getUsedHeap(), metricLocation, ts));
              client.print(metric("wifi_rssi", rssi, metricLocation, ts));

              // why not tempt fate and do a vsys measurement during a request handler?
              client.print(metric("vsys_voltage", measureVsys(), metricLocation, ts));
            }
          } else if(req.getPath() == "/wifi") {
            // output the current SSID
            printHead(client, 200, "text/html");
            client.println("<!DOCTYPE HTML>");
            if(req.getVerb() == Verb::Get){
              client.println("<head></head><body><pre>");
              client.println(getWifiStatus());
            }
            if(req.getVerb() == Verb::Post){
              String b = req.getBody();
              if(b.indexOf('\n') != -1){
                String newSsid = b.substring(0, b.indexOf('\n'));
                String newPass = b.substring(b.indexOf('\n')+1);
                client.println("SSID: " + String(currentSsid) + "\nPass: " + String(currentPass));
                settings.setWifiSettings(newSsid, newPass, currentSsid, currentPass);
                client.println("Successfully set wifi settings!");
                client.println("new ssid: " + newSsid + "\nnew pass: " + newPass);
                client.println("SSID: " + String(currentSsid) + "\nPass: " + String(currentPass));
              }
              client.println(getWifiStatus());
            }
          } else if(req.getPath() == "/location") {
            // output the current location
            printHead(client, 200, "text/html");
            client.println("<!DOCTYPE HTML>");
            client.println("<head></head><body><pre>");
            if(req.getVerb() == Verb::Post){
              String b = req.getBody();
              settings.setLocation(b, metricLocation);
              client.println("Successfully set location!");
            }
            client.println(metricLocation);
            client.println("</pre></body></html>");
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
    println("Client disconnected");
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
