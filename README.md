# Box Somfy DIO

## Introduction

Arduino program to control equipments via the protocol SOMFY RTS or Chacon DiO.

This code has been tested on ESP8266 with RF 433.42 Mhz transmitter. The
transmitter commonly sold have a 433.92 MHz crystal. You can buy 433.42 Mhz
crystal and change it with a bit of soldering.

## History

The code is based on the reverse engineering described in <https://pushstack.wordpress.com/somfy-rts-protocol/>.

The original code is <https://github.com/Nickduino/Somfy_Remote>.

There are many forks of this code on internet:

- <https://github.com/marmotton/Somfy_Remote>
- <https://github.com/RoyOltmans/somfy_esp8266_remote_arduino> that describes the required electronic parts.
- <https://www.amazon.fr/gp/product/B00G23NW6S>
- Many web sites describe the Chacon DIO protocol (<https://caron.ws/diy-cartes-microcontroleurs/piloter-vos-prises-chacon-rf433mhz-arduino/>).

This version is based on TOST Corp. code (2018 to 2020).

Modified by Stéphane Raimbault, 11/2020:

- MQTT Discovery for easy integration with Home Assistant
- a cover can be associated to many groups
- groups are exposed as remotes in Home Assistant
- simpler configuration (no rolling code, EEPROM addresses, etc)
- no dynamic allocation in loop() to avoid memory fragmentation
- faster code (remove many delays, fast path, pointers, pure string management, early return in functions)
- removed useless Ticker
- comments in english
- remove code to publish setup on weird server
- remove publishing of every handled commands on MQTT box topic
- DRY on many parts of the code
- common hostname/MQTT ID/MQTT topic for the box generated from MAC address

## License

[License CC BY NC SA](https://creativecommons.org/licenses/by-nc-sa/4.0/).

## Compilation and upload in ESP8266

Copy the file `config-default.h` to `config.h` and edit it for your settings.

In the Arduino IDE, add support for the board:

1. `File > Preferences`
2. in field `Additional Boards Manager URLs`: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. in the menu `Tools > Board > Boards manager`, install the board ESP8266 [version 2.4.1 to have the board WeMos D1 R2 & mini]. Don't use the last version 2.7.4!
4. select the board WeMos D1 R2 & mini ou LOLIN (WEMOS) D1 R2 & mini.
5. select the Port and speed 115200 bauds of the serial monitor.

## How to pair a SOMFY cover

After downloading the code in your box:

1. Press the PROG button on the remote control of one of the your equipment, the
   shutter will make a back and forth movement up/down
2. Send a "p" message to the MQTT server with the corresponding topic to the
equipment
3. Send a "u", "d" or "s" message to raise, lower or stop the shutter.

The rolling code value is stored in the EEPROM, so that you don't loose count of your rolling code after a power off or reset.
