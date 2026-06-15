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
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <time.h>
#include <SPI.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include "mbedtls/sha256.h"
#include "esp_ota_ops.h"

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

WebServer server(80);

#define PREF_NAME "ESPClock"

#define NO_NTP_WARN_SEC (90 * 60)
#define NO_NTP_REBOOT_SEC (120 * 60)

// 16 MHz
#define SPI_SPEED (16000000)

#define PIN_BUTTON 4
#define PIN_MOSI 3
#define PIN_MISO 1
#define PIN_SCK 0
#define PIN_SS 10

// The filename for the firmware update
#define FW_FILENAME "/update.bin"

// The magic header for a firmware update file
uint8_t MAGIC[] = {0x0d, 0xce, 0x71, 0xc9};

void write_reg(const unsigned char reg, const unsigned char val)
{
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_SS, LOW);
  SPI.transfer(reg);
  SPI.transfer(val);
  digitalWrite(PIN_SS, HIGH);
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

void showUpdate()
{
  write_reg(MAX_REG_DEC_MODE, 0);
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, MASK_C | MASK_D | MASK_E); // u
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, MASK_A | MASK_B | MASK_E | MASK_F | MASK_G); // P
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, MASK_B | MASK_C | MASK_D | MASK_E | MASK_G); // d
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, MASK_A | MASK_B | MASK_C | MASK_E | MASK_F | MASK_G); // A
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, MASK_D | MASK_E | MASK_F | MASK_G); // t
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, MASK_A | MASK_D | MASK_E | MASK_F | MASK_G); // E
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, MASK_A); // The start of the progress race
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, 0); // blank
}

void showFail(int code)
{
  write_reg(MAX_REG_DEC_MODE, 1 << DIGIT_100_MSEC); // decode the 10th digit
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_HR, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_HR, MASK_D | MASK_E | MASK_F | MASK_G); // F
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_MIN, MASK_A | MASK_B | MASK_D | MASK_E | MASK_F | MASK_G); // A
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_MIN, MASK_B); // i
  write_reg(MAX_REG_MASK_BOTH | DIGIT_10_SEC, MASK_D | MASK_E | MASK_F); // L
  write_reg(MAX_REG_MASK_BOTH | DIGIT_1_SEC, 0); // blank
  write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, code); // fail reason
  write_reg(MAX_REG_MASK_BOTH | DIGIT_MISC, 0); // blank
}

char serverFinished = 0;

void handleRoot()
{
  String html;
  if (strlen(ssid) == 0) strcpy(ssid, "default");
  if (strlen(hostname) == 0) strcpy(hostname, "ESPClock");
  if (strlen(ntp_server1) == 0) strcpy(ntp_server1, "0.us.pool.ntp.org");
  if (strlen(timezone) == 0) strcpy(timezone, "UTC0");
  if (brightness > 15) brightness = 15;
  if (colon_mode > 2) colon_mode = 1;
  
  html += "<html><head>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "<title>ESP Clock Setup</title>\n";
  html += "<style>\n";
  html += "  body { font-family: system-ui, sans-serif; padding: 20px; line-height: 1.5; }\n";
  html += "  input, select, button { width: 100%; padding: 12px; margin: 10px 0; font-size: 1rem; }\n";
  html += "  button { background: #0066cc; color: white; border: none; border-radius: 8px; }\n";
  html += "</style>\n";
  html += "<script>\n";
  html += "function tzselected() {\n";
  html += "  var tz = document.getElementById(\"tzpicker\").value;\n";
  html += "  if (!tz) return;\n";
  html += "  document.getElementById(\"tzline\").value = tz;";
  html += "}\n";
  html += "</script>\n";
  html += "</head>\n";
  html += "<body><form action=\"/submit\">\n";
  html += "<label for=\"hostname\">My hostname: </label><input name=\"hostname\" type=\"text\" value=\"";
  html += hostname; html += "\"><br>\n";
  html += "<label for=\"ssid\">WiFi SSID: </label><input name=\"ssid\" type=\"text\" value=\"";
  html += ssid; html += "\"><br>\n";
  html += "<label for=\"password\">WiFi password: </label><input name=\"password\" type=\"password\" value=\"";
  html += password; html += "\"><br>\n";
  html += "<label for=\"ntp_server\">NTP server: </label><input name=\"ntp_server\" type=\"text\" value=\"";
  html += ntp_server1; html += "\"><br>\n";
  html += "<label for=\"tzpicker\">Timezone: </label><select id=\"tzpicker\" onchange=\"tzselected()\">\n";
  html += "<option value="">Select from popular zones or enter a string below</option>\n";
  html += "<option value=\"UTC0\">UTC</option>\n";
  html += "<option value=\"PST8PDT,M3.2.0,M11.1.0\">Pacific time</option>\n";
  html += "<option value=\"MST7MDT,M3.2.0,M11.1.0\">Mountain time</option>\n";
  html += "<option value=\"MST7\">Mountain time (no DST)</option>\n";
  html += "<option value=\"CST6CDT,M3.2.0,M11.1.0\">Central time</option>\n";
  html += "<option value=\"EST5EDT,M3.2.0,M11.1.0\">Eastern time</option>\n";
  html += "</select><br>\n";
  html += "<label for=\"timezone\">Timezone string: </label><input name=\"timezone\" id=\"tzline\" type=\"text\" value=\"";
  html += timezone; html += "\"><br>\n";
  
  html += "<label for=\"ampm\">Clock style: </label><select name=\"ampm\"><option value=\"1\"";
  if (ampm) html += " selected";
  html += ">12 hour</option>\n";
  html += "<option value=\"0\"";
  if (!ampm) html += " selected";
  html += ">24 hour</option></select><br>\n";

  html += "<label for=\"tenth_enable\">Tenth-of-a-second digit: </label><select name=\"tenth_enable\"><option value=\"1\"";
  if (tenth_enable) html += " selected";
  html += ">on</option>\n";
  html += "<option value=\"0\"";
  if (!tenth_enable) html += " selected";
  html += ">off</option></select><br>\n";

  html += "<label for=\"colon_mode\">Colon style: </label><select name=\"colon_mode\"><option value=\"0\"";
  if (colon_mode == 0) html += " selected";
  html += ">off</option>\n";
  html += "<option value=\"1\"";
  if (colon_mode == 1) html += " selected";
  html += ">on</option>\n";
  html += "<option value=\"2\"";
  if (colon_mode == 2) html += " selected";
  html += ">blinking</option></select><br>\n";

  html += "<label for=\"brightness\">Brightness: </label><select name=\"brightness\">";
  for(int i = 0; i <= 15; i++)
  {
    html += "<option value=\"";
    html += i;
    html += "\"";
    if (brightness == i) html += " selected";
    html += ">";
    html += (i + 1);
    html += "</option>\n";
  }
  html += "</select><br>\n";

  html += "<input type=\"submit\" name=\"Save\">\n";
  html += "</form><p>\n";
  html += "<form action=\"/update\" method=\"POST\" enctype=\"multipart/form-data\">\n";
  html += "<label for=\"firmware\">Firmware update file: </label><input type=\"file\" name=\"firmware\" id=\"firmware\" required><br>\n";
  html += "<input type=\"submit\" value=\"Upload and apply FW update\">\n";
  html += "</form></body></html>\n";

  server.send(200, "text/html", html);
}

static File updateFile;           // Keep the file open across upload chunks
static bool uploadFailed = false;

void handleFirmwareUpload()
{
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("Upload started");
    uploadFailed = false;

    if (!SPIFFS.begin(true)) {           // true = auto-format if mount fails
        Serial.println("SPIFFS Mount Failed");
    } else {
        Serial.println("SPIFFS mounted successfully");
        Serial.printf("Total space: %u bytes\n", SPIFFS.totalBytes());
        Serial.printf("Used space:  %u bytes\n", SPIFFS.usedBytes());
    }
        // Remove any previous update file
    if (SPIFFS.exists(FW_FILENAME)) {
        SPIFFS.remove(FW_FILENAME);
    }

        // Open file for writing
    updateFile = SPIFFS.open(FW_FILENAME, FILE_WRITE);
    if (!updateFile) {
        Serial.println("Failed to open file for writing");
        uploadFailed = true;
    }
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
      if (updateFile && !uploadFailed) {
          if (updateFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Serial.println("Write error during upload");
                uploadFailed = true;
          }
      }  
  }
  else if (upload.status == UPLOAD_FILE_END) {
    Serial.println("Upload finished");
    if (updateFile) {
            updateFile.close();
    }
    Serial.printf("Upload finished. Total size: %u bytes\n", upload.totalSize);

    if (uploadFailed) {
            Serial.println("Upload failed");
    }
  }
}

char verifyUpdateDigest(String path, String& failMessage)
{
    File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        failMessage = "Could not open update file from SPIFFS";
        return false;
    }

    // === Read header ===
    uint8_t  magic[4];
    uint16_t version;
    uint16_t digestLen;

    if (f.read(magic, 4) != 4 ||
        f.read((uint8_t*)&version, 2) != 2 ||
        f.read((uint8_t*)&digestLen, 2) != 2)
    {
        f.close();
        failMessage = "Failed to read update header";
        return false;
    }

    // Basic validation
    if (memcmp(magic, MAGIC, 4) != 0) {
        f.close();
        failMessage = "Invalid magic number in update file";
        return false;
    }

    if (version != 0)
    {
        f.close();
        failMessage = "Invalid version in update file";
        return false;
    }

    digestLen = ntohs(digestLen); // fix it
    if (digestLen != 32) { // XXX this is specific to SHA-256.
        f.close();
        failMessage = "Unexpected digest length";
        return false;
    }

    uint8_t storedDigest[digestLen];
    if (f.read(storedDigest, digestLen) != digestLen)
    {
        f.close();
        failMessage = "Failed to read digest";
        return false;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);   // 0 = SHA-256

    f.seek(0); // go back to the start.

    uint8_t buffer[512];
    int bytesRead;

    if (f.read(buffer, 4 + 2) != 4 + 2)
    {
      failMessage = "Failed to read header for checksum";
      return false;
    }
    mbedtls_sha256_update(&ctx, buffer, 4 + 2);
    f.seek(f.position() + 2 + digestLen); // skip over the digest and its length
    
    // Now read the rest of the file
    while ((bytesRead = f.read(buffer, sizeof(buffer))) > 0) {
        mbedtls_sha256_update(&ctx, buffer, bytesRead);
    }

    uint8_t computedDigest[32];
    mbedtls_sha256_finish(&ctx, computedDigest);
    mbedtls_sha256_free(&ctx);
    f.close();

    // === Compare digests ===
    if (memcmp(computedDigest, storedDigest, 32) != 0) {
        failMessage = "Digest verification failed (file corrupted or tampered)";
        return false;
    }

    return true;
}

char applyFirmwareUpdate()
{
  const esp_partition_t* updateApp = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);

  if (!updateApp)
      return false;
  esp_ota_set_boot_partition(updateApp);
  esp_restart();
  while(true);
}

void handleUpdate()
{
  String html;
  String failMessage;

  if (uploadFailed)
  {
    failMessage = "Upload failed";
    goto bad;
  }
  // === Verify the digest ===
  if (!verifyUpdateDigest(FW_FILENAME, failMessage))
  {
    goto bad;
  }

  html += "<html><head><title>Update successful</title>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "</head>\n";
  html += "<body>The firmware upload was successful. The clock will now update and restart.</body></html>\n";
  server.send(200, "text/html", html);
  delay(500);

    // === Apply the chunks to flash ===
  applyFirmwareUpdate(); // If this returns, fall through to bad

  bad:
  html += "<html><head><title>Update failed</title>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "</head>\n";
  html += "<body>The update failed: ";
  html += failMessage;
  html += ". Go back and try again.</body></html>\n";
  server.send(200, "text/html", html);
  return;
}

void handleSubmit()
{
  String html;
  int ampm_val, tenth_enable_val, colon_mode_val, brightness_val;

  String Shostname = server.arg("hostname");
  String Sssid = server.arg("ssid");
  String Spassword = server.arg("password");
  String Sntp_server = server.arg("ntp_server");
  String Stimezone = server.arg("timezone");
  String Sampm = server.arg("ampm");
  String Stenth_enable = server.arg("tenth_enable");
  String Scolon_mode = server.arg("colon_mode");
  String Sbrightness = server.arg("brightness");
  if (Shostname.isEmpty()) goto bad;
  if (Sssid.isEmpty()) goto bad;
  if (Spassword.isEmpty()) goto bad;
  if (Sntp_server.isEmpty()) goto bad;
  if (Stimezone.isEmpty()) goto bad;
  if (Sampm.isEmpty()) goto bad;
  if (Stenth_enable.isEmpty()) goto bad;
  if (Scolon_mode.isEmpty()) goto bad;
  if (Sbrightness.isEmpty()) goto bad;
  ampm_val = Sampm.toInt();
  tenth_enable_val = Stenth_enable.toInt();
  colon_mode_val = Scolon_mode.toInt();
  brightness_val = Sbrightness.toInt();
  if (ampm_val < 0 || ampm_val > 1) goto bad;
  if (tenth_enable_val < 0 || tenth_enable_val > 1) goto bad;
  if (colon_mode_val < 0 || colon_mode_val > 2) goto bad;
  if (brightness_val < 0 || brightness_val > 15) goto bad;

  preferences.begin(PREF_NAME, false);
  preferences.putString("hostname", Shostname);
  preferences.putString("ssid", Sssid);
  preferences.putString("password", Spassword);
  preferences.putString("ntp_server", Sntp_server);
  preferences.putString("timezone", Stimezone);
  preferences.putBool("ampm", ampm_val);
  preferences.putBool("tenth_enable", tenth_enable_val);
  preferences.putInt("colon_mode", colon_mode_val);
  preferences.putInt("brightness", brightness_val);
  preferences.end();

  html += "<html><head><title>Preferences saved</title>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "</head>\n";
  html += "<body>Preferences have been saved. The clock will now restart.</body></html>\n";
  server.send(200, "text/html", html);
  serverFinished = 1;
  return;

  bad:
  html += "<html><head><title>Bad arguments</title>\n";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  html += "</head>\n";
  html += "<body>Arguments are bad. Go back and try again.</body></html>\n";
  server.send(200, "text/html", html);
  return;

}

void handleNotFound()
{
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
}

void setupButton()
{
  Serial.println("Setup pressed\n");
  showSetup();
  NTP.stop();
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESPClock");
  if (!MDNS.begin(hostname))
  {
    Serial.println("Failed to register mDNS hostname!");
  }
  DNSServer dnsServer;
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/submit", handleSubmit);
  server.on("/update", HTTP_POST, handleUpdate, handleFirmwareUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  while(!serverFinished)
  {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  delay(1000);
  ESP.restart();
  while(true);
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("=== MAIN APP STARTED ===");
  Serial.printf("BUTTON_PIN = GPIO %d\n", PIN_BUTTON);
  Serial.printf("SS = GPIO %d\n", PIN_SS);
  Serial.printf("SCK = GPIO %d\n", PIN_SCK);
  Serial.printf("MOSI = GPIO %d\n", PIN_MOSI);
  Serial.printf("MISO = GPIO %d\n", PIN_MISO);

  // Reset system time to zero so that now.tv_sec acts as uptime until first NTP sync
  struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  pinMode(PIN_SS, OUTPUT);
  digitalWrite(PIN_SS, HIGH);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
  write_reg(MAX_REG_CONFIG, MAX_REG_CONFIG_R | MAX_REG_CONFIG_S);
  write_reg(MAX_REG_SCAN_LIMIT, 7); // display all 8 digits

  write_reg(MAX_REG_TEST, 1);
  delay(1000);
  write_reg(MAX_REG_TEST, 0);

  preferences.begin(PREF_NAME, true);
  if (!preferences.isKey("hostname"))
  {
    // Set up the defaults for the string properties
    preferences.end();
    preferences.begin(PREF_NAME, false);
    preferences.putString("hostname", "espclock");
    preferences.putString("ssid", "none");
    preferences.putString("ntp_server", "0.us.pool.ntp.org");
    preferences.putString("timezone", "PST8PDT,M3.2.0,M11.1.0");
    preferences.end();
    preferences.begin(PREF_NAME, true);
  }
  preferences.getString("hostname", hostname, sizeof(hostname));
  preferences.getString("ssid", ssid, sizeof(ssid));
  preferences.getString("password", password, sizeof(password));
  preferences.getString("ntp_server", ntp_server1, sizeof(ntp_server1));
  preferences.getString("timezone", timezone, sizeof(timezone));
  ampm = preferences.getBool("ampm", true);
  tenth_enable = preferences.getBool("tenth_enable", true);
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
    if (digitalRead(PIN_BUTTON) == LOW)
    {
      setupButton();
    }
    Serial.print(".");
    write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, _BV(i--));
    if (i <= 0) i = 6;
    delay(100);
  }
  write_reg(MAX_REG_CONFIG, MAX_REG_CONFIG_R | MAX_REG_CONFIG_S);
 
  Serial.println("\nConnected");
  
  if (!MDNS.begin(hostname))
  {
    Serial.println("Failed to register mDNS hostname!");
  }

  NTP.setTimeZone(timezone);
  NTP.begin(ntp_server1, true);
}

int last_tenth = 99;

void loop() {
  // put your main code here, to run repeatedly:

  if (digitalRead(PIN_BUTTON) == LOW)
  {
    setupButton();
  }

  timeval now;
  gettimeofday(&now, NULL);
  int tenth = now.tv_usec / 100000;
  if (last_tenth == tenth)
    return;
  last_tenth = tenth;

  time_t lastSync = NTP.getLastNTPSync();
  if (lastSync == 0 || ((now.tv_sec - lastSync) > NO_NTP_WARN_SEC)) {
    showNoNtp();
    
    if ((now.tv_sec - lastSync) > NO_NTP_REBOOT_SEC) {
      Serial.println("No NTP for too long. Rebooting.");
      delay(1000);
      ESP.restart();
      while(true);
    }
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

/*
  char buf[64];
  snprintf(buf, sizeof(buf), "%02d, %02d, %02d, %01d %s", hour, lt->tm_min, lt->tm_sec, now.tv_usec / 100000, isPm?"PM":"AM");
  Serial.println(buf);
*/

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
