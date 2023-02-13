# Arduino BME688 Pi Pico W sketch

This sketch allows you to connect a BME688 I2C breakout board to a Raspberry Pi Pico W and expose a
web server that will serve up an HTML table with the current measurements.

## Features

### Metrics
It supports prometheus-style (scrape) metrics collection for inclusion in a timeseries database. It
also supports updating a Homebridge instance configured with the
[homebridge-http-webhooks plugin](https://github.com/benzman81/homebridge-http-webhooks).

### OTA updates
The sketch supports ArduinoOTA, allowing you to update the sketch from the Arduino IDE over WiFi.

### Wifi AP mode
If it cannot connect to the wifi SSID configured in the onboard flash or embedded in the secrets.h
file, it will create its own wireless network and serve the metrics page over that.

### Dynamic settings
#### Wifi 
To see the currently-configured wifi settings, make a `GET` request to the `/wifi` route. If you
would like to change the SSID and/or password you may do so by making a POST to the `/wifi` route.
The post body should be a string in the format "<SSID>\n<password>".  A successful POST will
persist the new SSID and password to onboard flash memory. It will not automatically connect to the
new wifi network until either restarted or a POST is made to the `/reconnect` route.

#### Metric location
If you would like to change the location embedded in the prometheus metrics and Homebridge sensor,
you may POST a string payload to the `/location` endpoint. A successful POST will persist the
location to onboard flash memory and immediately update the location in Homebridge webhooks and in
the metrics scraped by Prometheus.

#### Others
- The sketch currently requires you to hardcode the homebridge hostname and port, as well as the
  virtual sensor names. These will be configurable in a future release.
- The TLS certificate is currently hardcoded. It will be configurable in the future.
- The temperature and pressure offsets are currently hardcoded. These will be configurable in a
  future release.

### OLED display
If an I2C OLED display is connected at address 0x3c, it will attempt to display the latest
completed measurement data on the screen, as well as the wifi signal quality (or SSID if in AP
mode).

## Requirements
- A [Raspberry Pi Pico W](https://www.adafruit.com/product/5526)
- A [BME688 I2C breakout](https://www.adafruit.com/product/5046)

## Optional add-ons
- A [Pico LiPo SHIM](https://www.adafruit.com/product/5612) and [LiPo battery](https://www.adafruit.com/product/3898)
- An [OLED display](https://www.adafruit.com/product/938)
