#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

extern "C" int fable_main(int argc, char **argv);

void setup() {
    Serial.begin(115200);
    delay(500);

    /* PSRAM check */
    if (psramFound()) {
        Serial.printf("PSRAM: %u bytes total, %u free\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("No PSRAM detected");
    }

    /* SD card */
    SPI.begin(14, 2, 12, 13);
    if (SD.begin(13)) {
        Serial.println("SD mounted");
    } else {
        Serial.println("SD mount failed");
    }

    char *argv[] = { (char*)"fable", nullptr };
    fable_main(1, argv);
}

void loop() { }