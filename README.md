This is the source code for https://hackaday.io/project/204531-esp32-ntp-clock.

It requires the ESPNtpClient library, but that library has a crash bug in it at the moment. The fork here: https://github.com/Nourbakhsh-Rad/ESPNtpClient works correctly.

The code is intended to be used on an ESP32-C3-Mini-1 module, with SPI connected to a MAX6951. IO pin 4 has a button that will put the clock in a setup mode, which creates a WiFi AP and puts up a web page to allow configuring.

To build this, you need to take the two .ino files and put them and the two partition.csv files into two different sketch folders in the Arduino IDE (the partitions.csv file is for the main sketch, partitions-updater.csv is for the updater). Select the "ESP32C3 Dev" board. Turn on USB CDC and select a custom partition map.

Build and install the main app with the IDE in the usual manner. For the updater, you need to do that manually. Export the compiled binary, then upload it with "esptool write_flash 0x1A0000 ESP_Clock_Updater.ino.bin"
