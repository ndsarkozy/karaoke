#include "module_test.h"
#include "display.h"
#include <Arduino.h>

void Module_Test_Init(void) {
    #ifdef DISPLAY_TEST
    display_init();
    #endif
}

void Module_Test_Run(void) {
    #ifdef DISPLAY_TEST
    Display_Test();
    #endif
}

#ifdef DISPLAY_TEST
void Display_Test(void) {
    Serial.println("[TEST] Display ───────────────");
    display_fill(COLOR_RED);
    delay(500);
    display_fill(COLOR_GREEN);
    delay(500);
    display_fill(COLOR_BLUE);
    delay(500);
    display_clear();
    display_showMessage("KARAOKE", "ESP32", COLOR_WHITE);
    Serial.println("[TEST] Display PASS - check screen");
}
#endif
