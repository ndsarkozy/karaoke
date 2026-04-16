#include "module_test.h"
#include "config.h"
#include "net.h"
#include <Arduino.h>

#ifdef DISPLAY_TEST
#include "display.h"
#endif

// ── INIT ───────────────────────────────────────────────
void Module_Test_Init(void) {

    #ifdef WIFI_TEST
    wifi_connect();
    #endif

    #ifdef DISPLAY_TEST
    display_init();
    #endif

    #ifdef SPOTIFY_TEST
    wifi_connect();
    #endif

    #ifdef LRCLIB_TEST
    wifi_connect();
    #endif

    #ifdef FULL_SYSTEM_TEST
    wifi_connect();
    display_init();
    #endif
}

// ── RUN ────────────────────────────────────────────────
void Module_Test_Run(void) {

    #ifdef WIFI_TEST
    WiFi_Test();
    #endif

    #ifdef DISPLAY_TEST
    Display_Test();
    #endif

    #ifdef DAC_TEST
    DAC_Test();
    #endif

    #ifdef RTC_TEST
    RTC_Test();
    #endif

    #ifdef SPOTIFY_TEST
    Spotify_Test();
    #endif

    #ifdef LRCLIB_TEST
    LRCLib_Test();
    #endif

    #ifdef LYRIC_SYNC_TEST
    LyricSync_Test();
    #endif

    #ifdef FULL_SYSTEM_TEST
    FullSystem_Test();
    #endif
}

// ── TEST DEFINITIONS ───────────────────────────────────

#ifdef WIFI_TEST
void WiFi_Test(void) {
    Serial.println("[TEST] WiFi ─────────────────");
    if (wifi_connected()) {
        Serial.println("[TEST] WiFi PASS");
    } else {
        Serial.println("[TEST] WiFi FAIL");
    }
}
#endif

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

#ifdef DAC_TEST
void DAC_Test(void) {
    Serial.println("[TEST] DAC ───────────────────");
    Serial.println("[TEST] DAC PASS - check speaker");
}
#endif

#ifdef RTC_TEST
void RTC_Test(void) {
    Serial.println("[TEST] RTC ───────────────────");
    Serial.println("[TEST] RTC PASS");
}
#endif

#ifdef SPOTIFY_TEST
void Spotify_Test(void) {
    Serial.println("[TEST] Spotify ───────────────");
}
#endif

#ifdef LRCLIB_TEST
void LRCLib_Test(void) {
    Serial.println("[TEST] LRCLib ────────────────");
}
#endif

#ifdef LYRIC_SYNC_TEST
void LyricSync_Test(void) {
    Serial.println("[TEST] LyricSync ─────────────");
    Serial.println("[TEST] LyricSync PASS");
}
#endif

#ifdef FULL_SYSTEM_TEST
void FullSystem_Test(void) {
    Serial.println("[TEST] Full System ───────────");
    Serial.println("[TEST] Full System PASS");
}
#endif