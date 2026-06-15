#include <SPIFFS.h>
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include <SPI.h>

#define _BV(x) (1 << (x))
#include "max6951.h"

#define SPI_SPEED (16000000)

#define PIN_BUTTON 4
#define PIN_MOSI 3
#define PIN_MISO 1
#define PIN_SCK 0
#define PIN_SS 10

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

void applyFirmwareUpdate(const char* path)
{
  uint8_t numChunks;
  uint8_t progress = 6;

  showUpdate();
  File f = SPIFFS.open(path, FILE_READ);
    if (!f) {
        showFail(0);
        goto bad;
    }

    uint8_t magic[4];
    uint16_t version;
    uint16_t digestLen;

    if (f.read(magic, 4) != 4 ||
        f.read((uint8_t*)&version, 2) != 2 ||
        f.read((uint8_t*)&digestLen, 2) != 2)
        {
          showFail(1);
          goto bad;
        }

    if (memcmp(magic, MAGIC, sizeof(MAGIC)) || version != 0)
    {
      showFail(2);
      goto bad;
    }
    digestLen = ntohs(digestLen);
    // Skip header (Magic + Version + DigestLen + Digest)
    f.seek(4 + 2 + 2 + digestLen);

    if (f.read(&numChunks, 1) != 1) {
        f.close();
        showFail(3);
        goto bad;
    }

    for (uint8_t i = 0; i < numChunks; i++) {
        uint32_t address;
        uint32_t length;

        if (f.read((uint8_t*)&address, 4) != 4 ||
            f.read((uint8_t*)&length, 4) != 4)
        {
            f.close();
            showFail(4);
            goto bad;
        }
        address = ntohl(address);
        length = ntohl(length);

        //Serial.printf("Flashing chunk %d @ 0x%08X (%u bytes)\n", i, address, length);
        //Serial.flush();
        //delay(10);

        // Erase the necessary sectors first (safer)
        size_t sectorSize = 4096;
        uint32_t startSector = address / sectorSize;
        uint32_t endSector   = (address + length + sectorSize - 1) / sectorSize;
        //Serial.printf("Sector size %d startsector %d endsector %d\n", sectorSize, startSector, endSector);
        //Serial.flush();
        //delay(10);

        for (uint32_t sec = startSector; sec < endSector; sec++) {
            //Serial.printf("Erasing sector %d\n", sec);
            //Serial.flush();
            //delay(10);
            write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, _BV(progress--));
            if (progress <= 0) progress = 6;
            esp_err_t err = esp_flash_erase_region(NULL, sec * sectorSize, sectorSize);
            if (err != ESP_OK) {
                f.close();
                showFail(5);
                goto bad;
            }
        }

        // Write the data in chunks
        const size_t writeSize = 4096;
        uint8_t buffer[writeSize];
        uint32_t remaining = length;
        uint32_t offset = 0;

        while (remaining > 0) {
            write_reg(MAX_REG_MASK_BOTH | DIGIT_100_MSEC, _BV(progress--));
            if (progress <= 0) progress = 6;

            size_t toRead = (remaining > writeSize) ? writeSize : remaining;
            int bytesRead = f.read(buffer, toRead);
            if (bytesRead <= 0) {
                f.close();
                showFail(6);
                goto bad;
            }
            //Serial.printf("Writing to %lx...\n", address + offset);
            //Serial.flush();
            //delay(10);

            esp_err_t err = esp_flash_write(NULL, buffer, address + offset, bytesRead);
            if (err != ESP_OK) {
                f.close();
                showFail(7);
                goto bad;
            }

            offset += bytesRead;
            remaining -= bytesRead;
        }
    }

    f.close();
    return;

bad:
    while(true);
}

void setup()
{
    //Serial.begin(115200);
    //delay(100);

    pinMode(PIN_SS, OUTPUT);
    digitalWrite(PIN_SS, HIGH);
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, PIN_SS);
    write_reg(MAX_REG_CONFIG, MAX_REG_CONFIG_R | MAX_REG_CONFIG_S);
    write_reg(MAX_REG_SCAN_LIMIT, 7); // display all 8 digits

    // 1. Always try to switch back to the main app first.
    //    This ensures that even if we crash later, the next boot goes back to the main app.
    const esp_partition_t* mainApp = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);

    if (!mainApp) {
      showFail(0xF);
      while(true);
    }
    esp_ota_set_boot_partition(mainApp);

    // 2. Mount SPIFFS and check for the update file
    if (!SPIFFS.begin(true)) {
        showFail(0xE);           // SPIFFS mount failed
        while (true);
    }

    if (!SPIFFS.exists(FW_FILENAME)) {
        // Nothing to do — reboot into the main app
        ESP.restart();
    }

    // 3. File exists — run the update
    applyFirmwareUpdate(FW_FILENAME);

    // If we get here, the update succeeded.
    // applyFirmwareUpdate() should have already switched the boot partition
    // back to the main app and we can just reboot.
    ESP.restart();
}

void loop()
{

}