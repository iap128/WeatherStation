# WeatherStation
This project creates an internet connected weather station that connects to a local SQL server and logs the data in a database. It's built using SparkFun's weather accessories and MicroMod controllers. The purpose of this project is to collect weather information that will then be displayed inside using another Arduino or on a website reading the SQL data created by the weather station. 

## Parts
- [Weather Meter Kit](https://www.sparkfun.com/products/15901)
- [MicroMod Weather Carrier Board](https://www.sparkfun.com/products/16794)
- [MicroMod ESP32 Processor](https://www.sparkfun.com/products/16781)

## Sensors
This weather station collects info from the following sensors:
- Temperature
- Humidity
- Pressure
- Wind speed
- Wind direction
- Precipitation quantity

## Setup
I'm using a number of libraries and packages to support these sensors.

### Board Setup
The board needs the USB to UART driver in order to connect properly. We also need to download the ESP32 board definitions. The board we're using from that package is the `SparkFun ESP32 MicroMod`.
1. [CP210x USB to UART Bridge VCP Drivers](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers?tab=downloads)
2. [ESP32 boards](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)

### Weather Meters
Install the following packages from the Arduino library:
1. SparkFun BME280
2. SparkFun VEML6075
3. SparkFun AS3935

### MariaDB SQL
This package is needed in order to connect the MicroMod to a MySQL server. I'm running MariaDB on my Synology server, thus why I chose this package. It's available in the Arduino library although I had issues trying to install it. I downloaded it manually instead. 
- [MySQL_MariaDB_Generic](https://github.com/khoih-prog/MySQL_MariaDB_Generic)
