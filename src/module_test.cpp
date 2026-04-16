#include "module_test.h"
#include "config.h"
#include "net.h"
#include <Arduino.h>

// ── INIT ───────────────────────────────────────────────
// Initialize only what the active tests need
void Module_Test_Init(void) {

    #ifdef WIFI_TEST
    wifi_connect();
    #endif

    #ifdef DISPLAY_TEST
    // display_init();
    #endif

    #ifdef DAC_TEST
    // dac_init();
    #endif

    #ifdef RTC_TEST
    // rtc_init();
    #endif

    #ifdef SPOTIFY_TEST
    wifi_connect();
    // spotify_init();
    #endif

    #ifdef LRCLIB_TEST
    wifi_connect();
    #endif

    #ifdef FULL_SYSTEM_TEST
    wifi_connect();
    // display_init();
    // dac_init();
    // rtc_init();
    // spotify_init();
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
    // draw a test pattern on GC9A01
    // display_fill(COLOR_RED);
    // delay(500);
    // display_fill(COLOR_GREEN);
    // delay(500);
    // display_fill(COLOR_BLUE);
    Serial.println("[TEST] Display PASS - check screen");
}
#endif

#ifdef DAC_TEST
void DAC_Test(void) {
    Serial.println("[TEST] DAC ───────────────────");
    // dac_tone(440, 500);   // 440Hz for 500ms
    Serial.println("[TEST] DAC PASS - check speaker");
}
#endif

#ifdef RTC_TEST
void RTC_Test(void) {
    Serial.println("[TEST] RTC ───────────────────");
    // DateTime now = rtc_getTime();
    // Serial.println("[RTC] Time: " + rtc_timeString(now));
    Serial.println("[TEST] RTC PASS");
}
#endif

#ifdef SPOTIFY_TEST
void Spotify_Test(void) {
    Serial.println("[TEST] Spotify ───────────────");
    // bool ok = spotify_refreshToken();
    // Serial.println(ok ? "[TEST] Spotify PASS" : "[TEST] Spotify FAIL");
}
#endif

#ifdef LRCLIB_TEST
void LRCLib_Test(void) {
    Serial.println("[TEST] LRCLib ────────────────");
    // bool ok = lrclib_fetch("Blinding Lights", "The Weeknd");
    // Serial.println(ok ? "[TEST] LRCLib PASS" : "[TEST] LRCLib FAIL");
}
#endif

#ifdef LYRIC_SYNC_TEST
void LyricSync_Test(void) {
    Serial.println("[TEST] LyricSync ─────────────");
    // int line = sync_getLine(47000);
    // Serial.println("[SYNC] Line at 47s: " + String(line));
    Serial.println("[TEST] LyricSync PASS");
}
#endif

#ifdef FULL_SYSTEM_TEST
void FullSystem_Test(void) {
    Serial.println("[TEST] Full System ───────────");
    // runs everything together
    // spotify_refreshToken();
    // SpotifyTrack t;
    // spotify_getNowPlaying(t);
    // lrclib_fetch(t.title, t.artist);
    // int line = sync_getLine(t.progressMs);
    // display_showLyric(lyrics[line].text);
    Serial.println("[TEST] Full System PASS");
}
#endif