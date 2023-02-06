#if !defined(secrets_h)
#define secrets_h
// Enter your WiFi SSID and password
const char ssid[] = "your-ssid"; // network SSID (name)
const char pass[] = "your-super-secure-password"; // WPA network password

/**
 * If you would like to update an apple home temperature "sensor"
 * emulated by https://github.com/benzman81/homebridge-http-webhooks,
 * define these constants
 */
#define HOMEBRIDGE
const char homebridgeHost[] = "192.168.192.168";
const char webhookPort[] = "51828";
const char aqiSensorId[] = "aqi";
const char humiditySensorId[] = "humidity";
const char tempSensorId[] = "temp";

/**
 * If you have a calibrated thermometer, use it to determine the temp offset (in c)
 */
const float temperatureOffset = 0.5;

/**
 * If you would like to serve the web server over TLS in 
 * addition to plain http, define your server certificate 
 * chain (leaf, intermediate(s), and root) below, along with
 * your private key. If you are using an ECC cert, define
 * TLS_ECC as well.
 */
#define TLS
// SSL certificate
#define TLS_ECC
const char serverCert[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";

const char serverKey[] PROGMEM = R"EOF(
-----BEGIN PRIVATE KEY-----
...
-----END PRIVATE KEY-----
)EOF";
#endif
