# Greenhouse-controller
A cheap but efficient controller for small-scale farmers looking to automate their greenhouse.
Allow control on : 
- 3 time-zones with different temperature settings and activation temperature for each devices
- 2 rollups opening and closing simultaneously in 4 steps, with pause in between to allow air flow
- 1 fan starting when a specific temperature is reached (depending of the time-zone)
- 1 heater starting when a specific temperature is reached (depending of the time-zone)
- 1 second heater starting when a specific temperature is reached (depending of the time-zone)

Material needed : 
- arduino uno
- dht11 temperature & humidity sensor
- [DS3231 Real-time-clock module](https://abra-electronics.com/robotics-embedded-electronics/breakout-boards/clocks/ard-ds3231-super-accurate-real-time-clock-ds3231.html)
- [i2c 20x4 LCD from DFRobot](https://www.dfrobot.com/wiki/index.php/I2C_TWI_LCD2004_Module_(Arduino/Gadgeteer_Compatible))
- [8 optoisolated relay module from Sainsmart](http://www.sainsmart.com/8-channel-dc-5v-relay-module-for-arduino-pic-arm-dsp-avr-msp430-ttl-logic.html)
- jumpers and wires

Version of arduino IDE : 1.6.12

Libraries included :
[Adafruit Unified Sensor Library](https://github.com/adafruit/Adafruit_Sensor)
[DHT Sensor Library](https://github.com/adafruit/DHT-sensor-library)
[NewliquidCrystal v.1.3.4](https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads)
