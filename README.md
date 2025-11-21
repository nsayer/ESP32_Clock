This is the source code for https://hackaday.io/project/204531-esp32-ntp-clock.

It requires the ESPNtpClient library, but that library has a crash bug in it at the moment. The fork here: https://github.com/Nourbakhsh-Rad/ESPNtpClient works correctly.

The code is intended to be used on an ESP32-C3-Mini-1 module, with SPI connected to a MAX6951. IO pin 4 has a button that will put the clock in a setup mode, which creates a WiFi AP and puts up a web page to allow configuring.
