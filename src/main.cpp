#include <Arduino.h>
#include "config.h"
#include "module_test.h"

#define RUN_TESTS

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Karaoke ESP32 Boot ===");

    #ifdef RUN_TESTS
    Module_Test_Init();
    #endif
}

void loop() {
    #ifdef RUN_TESTS
    Module_Test_Run();
    #endif
}