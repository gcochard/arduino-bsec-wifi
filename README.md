# Arduino BME688 Pi Pico W sketch

This sketch allows you to connect a BME688 I2C breakout board to a Raspberry Pi Pico W
and expose a web server that will serve up an HTML table with the current measurements.

## Features
It supports prometheus-style (scrape) metrics collection for inclusion in a timeseries
database. It also supports updating a Homebridge instance configured with the
[homebridge-http-webhooks plugin](https://github.com/benzman81/homebridge-http-webhooks).

The sketch additionally supports ArduinoOTA, allowing you to update the sketch from the
Arduino IDE remotely over WiFi.

If it cannot connect to the wifi SSID embedded in the secrets.h file, it will create
its own wireless network and serve the metrics page over that. Support for dynamically
reconfiguring the wifi network and saving/loading networks from flash memory is planned.

If an I2C OLED display is connected at address 0x3c, it will attempt to display the
latest completed measurement data on the screen, as well as the wifi signal quality
(or SSID if in AP mode).

## Requirements
- A [Raspberry Pi Pico W](https://www.adafruit.com/product/5526)
- A [BME688 I2C breakout](https://www.adafruit.com/product/5046)

## Optional add-ons
- A [Pico LiPo SHIM](https://www.adafruit.com/product/5612) and [LiPo battery](https://www.adafruit.com/product/3898)
- An [OLED display](https://www.adafruit.com/product/938)
