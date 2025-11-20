/*

 ESP32 Clock for Arduino
 Copyright 2025 Nicholas W. Sayer
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License along
 with this program; if not, write to the Free Software Foundation, Inc.,
 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <ESPNtpClient.h>
#include <WiFi.h>
#include <time.h>
#include <SPI.h>
#include <Preferences.h>

#define _BV(x) (1 << (x))
#include "max6951.h"

// These are things we're going to configure
char hostname[64];
char ssid[64];
char password[64];
char ntp_server1[64];
char timezone[64];
char ampm, tenth_enable, brightness, colon_mode;

Preferences preferences;

#define PREF_NAME "ESPClock"

// 16 MHz
#define SPI_SPEED (16000000)

#define BUTTON_PIN 4

void write_reg(const unsigned char reg, const unsigned char val)
{
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(SS, LOW);
  SPI.transfer(reg);
  SPI.transfer(val);
  digitalWrite(SS, HIGH);
  SPI.endTransaction();
}

void showNoNtp()
{
  write_reg(MAX_REG_DEC_MODE, 0);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, MASK_C | MASK_E | MASK_G); // n
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, MASK_C | MASK_D | MASK_E | MASK_G); // o
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, MASK_C | MASK_E | MASK_G); // n
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, MASK_D | MASK_E | MASK_F | MASK_G); // t
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, MASK_A | MASK_B | MASK_E | MASK_F | MASK_G); // P
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, 0); // blank
}

void showConnect()
{
  write_reg(MAX_REG_DEC_MODE, 0);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, MASK_A | MASK_D | MASK_E | MASK_F); // C
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, MASK_C | MASK_D | MASK_E | MASK_G); // o
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, MASK_C | MASK_E | MASK_G); // n
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, MASK_A | MASK_D | MASK_E | MASK_F | MASK_G); // E
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, MASK_A | MASK_D | MASK_E | MASK_F); // C
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, MASK_D | MASK_E | MASK_F | MASK_G); // t
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, 0); // blank
}

void showSetup()
{
  write_reg(MAX_REG_DEC_MODE, 0);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, MASK_A | MASK_C | MASK_D | MASK_F | MASK_G); // S
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, MASK_A | MASK_D | MASK_E | MASK_F | MASK_G); // E
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, MASK_D | MASK_E | MASK_F | MASK_G); // t
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, MASK_C | MASK_D | MASK_E); // u
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, MASK_A | MASK_B | MASK_E | MASK_F | MASK_G); // P
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, 0); // blank
}

void setPreferences()
{
  preferences.begin(PREF_NAME, false);
  preferences.putString("hostname", "espclock");
  preferences.putString("ssid", "KFU-NET");
  preferences.putString("password", "DTW2SJC!!");
  preferences.putString("ntp_server", "0.us.pool.ntp.org");
  preferences.putString("timezone", "EST5EDT,M3.2.0,M11.1.0");
  preferences.putBool("ampm", true);
  preferences.putBool("tenths", true);
  preferences.putInt("brightness", 15);
  preferences.putInt("colon_mode", 1);
  preferences.end();
  Serial.println("Preferences written\n");
  while(true);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //setPreferences();

  pinMode(SS, OUTPUT);
  digitalWrite(SS, HIGH);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  SPI.begin(SCK, MISO, MOSI, SS);
  write_reg(MAX_REG_CONFIG, MAX_REG_CONFIG_R | MAX_REG_CONFIG_S);
  write_reg(MAX_REG_SCAN_LIMIT, 7); // display all 8 digits

  write_reg(MAX_REG_TEST, 1);
  delay(1000);
  write_reg(MAX_REG_TEST, 0);

  preferences.begin(PREF_NAME, true);
  preferences.getString("hostname", hostname, sizeof(hostname));
  preferences.getString("ssid", ssid, sizeof(ssid));
  preferences.getString("password", password, sizeof(password));
  preferences.getString("ntp_server", ntp_server1, sizeof(ntp_server1));
  preferences.getString("timezone", timezone, sizeof(timezone));
  ampm = preferences.getBool("ampm", true);
  tenth_enable = preferences.getBool("tenths", true);
  brightness = preferences.getInt("brightness", 15);
  colon_mode = preferences.getInt("colon_mode", 1);
  preferences.end();
  write_reg(MAX_REG_INTENSITY, brightness);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, password);

  Serial.println("\nConnecting to WiFi Network ..");
  showConnect();

  int i = 6; 
  while(WiFi.status() != WL_CONNECTED){
    Serial.print(".");
    write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, _BV(i--));
    if (i <= 0) i = 6;
    delay(100);
  }
  write_reg(MAX_REG_CONFIG, MAX_REG_CONFIG_R | MAX_REG_CONFIG_S);
 
  Serial.println("\nConnected");

  NTP.setTimeZone(timezone);
  NTP.begin(ntp_server1, true);
}

int last_tenth = 99;

void loop() {
  // put your main code here, to run repeatedly:

  if (digitalRead(BUTTON_PIN) == LOW)
  {
    Serial.println("Setup pressed\n");
    showSetup();
    // and here we shunt over into the setup web UI
    // eventually;
    delay(5000);
    ESP.restart();
  }

  timeval now;
  gettimeofday(&now, NULL);
  int tenth = now.tv_usec / 100000;
  if (last_tenth == tenth)
    return;
  last_tenth = tenth;

  if (NTP.syncStatus() != 0) {
    Serial.println("No sync");
    showNoNtp();
    return;
  }

  struct tm* lt = localtime(&now.tv_sec);
  int hour = lt->tm_hour;
  int isPm = false;
  if (ampm) {
    if (hour > 12)
      hour -= 12, isPm = true;
    else if (hour == 12)
      isPm = true;
    else if (hour == 0)
      hour = 12;
  }

  char buf[64];
  snprintf(buf, sizeof(buf), "%02d, %02d, %02d, %01d %s", hour, lt->tm_min, lt->tm_sec, now.tv_usec / 100000, isPm?"PM":"AM");
  Serial.println(buf);

  unsigned char decode_mask = (unsigned char)(~_BV(DIGIT_MISC)); // All decode except the misc digit.
  if (ampm && hour < 10) {
    decode_mask &= ~_BV(DIGIT_10_HR); // for the 12 hour display, blank leading 0 for hour
  }
  if (!tenth_enable) {
    decode_mask &= ~_BV(DIGIT_100_MSEC); // turn off the tenth digit decode. We'll write a 0.
  }
  write_reg(MAX_REG_DEC_MODE, decode_mask);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, hour / 10);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, hour % 10);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, lt->tm_min / 10);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, lt->tm_min % 10);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, lt->tm_sec / 10);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, (lt->tm_sec % 10) | (tenth_enable?MASK_DP:0));
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, tenth_enable?tenth:0);

  unsigned char misc_digit = 0;
  if ((colon_mode == 1) || ((colon_mode == 2) && (now.tv_sec % 2 == 0))) {
    misc_digit |= MASK_COLON_HM | MASK_COLON_MS;
  }
  if (ampm) {
    misc_digit |= (isPm?MASK_PM:MASK_AM);
  }
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, misc_digit);
}
