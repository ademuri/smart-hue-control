# Smart Hue Control

## Motion Sensor

This is a WiFi-connected motion sensor that turns Hue lights on and off. It uses
an ESP32 and a PIR motion sensor. It's "smarter" than the Hue motion sensor -
if you manually change the lights, it won't stomp on your changes.

You can view the debugging dashboard at
[motion-sensor.local](http://motion-sensor.local/).

To use this, you'll need to copy `constants.sample.h` to `constants.h` and fill
out the fields. Follow the [Hue API
guide](https://developers.meethue.com/develop/get-started-2/core-concepts/) for
how to determine the address of your bridge and get a username.
