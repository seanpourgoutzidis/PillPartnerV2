# Pill Partner V2

This is an ESP32-based microcontroller project to remind my grandparents to take their medication on time. 
It takes pictures of their pill organizer for their morning, afternoon, and nightly dose and sends them to my phone.
It also allows me to send commands to the device to get on-demand pictures and sound a beep to remind them to take their pills.

This is the 2nd iteration of Pill Partner. Make sure to check out the first iteration I built in collaboration with [@charlottepfritz](https://github.com/charlottepfritz) at this [link](https://github.com/charlottepfritz/PillPartner)!

## Technical Explanation

Upon booting up, the device will set up the camera and connect to the Wifi. It will then get the time from an NTP server to initialize the state. 
Following this, the device will enter a state of deep sleep to conserve power (it will be battery powered).

There are two ways the device will wake up.

1. I've instructed my grandparents to press a button when they take a dose. This will take a picture and send it to my Telegram app.
2. The device is configured to wake up every 20 minutes. If they haven't taken a pill and the interval (morning, afternoon, or night) is about to end, it will send a picture and let me know the pill hasn't been taken.

I can send commands to the device as well:
* `/photo` - Send a picture to my phone the next time the ESP32 wakes up
* `/beep` - Make a beeping noise the next time the ESP32 wakes up.

## Hardware 

* ESP32 WRover with Camera module
* Buzzer (connected to GPIO 14)
* Button (connected to GPIO 33)
* Breadboard, 10k resistor, and jumper wires to connect button and buzzer to ESP32.
* Power source.

## Inspiration and Tutorials

* [External Wakeup from Deep Sleep](https://randomnerdtutorials.com/esp32-external-wake-up-deep-sleep/)
* [Timer Wakeup from Deep Sleep](https://randomnerdtutorials.com/esp32-timer-wake-up-deep-sleep/)
* [Get Date and Time from NTP Server](https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/)
* [Take Picture and Send to Telegram](https://randomnerdtutorials.com/telegram-esp32-cam-photo-arduino/)

## Demo

[Link to demo video](https://youtu.be/PeERiaaR3v0)

>[!NOTE]
> Please note that the device has not been installed and currently does not have a frame. 





