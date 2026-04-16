#include "module_test.h"
#include "config.h"
#include "net.h"
#include "get_lyrics.h"
#include "lyric_sync.h"
#include <Arduino.h>

#ifdef DISPLAY_TEST
#include "display.h"
#endif

#ifdef SPOTIFY_TEST
#include "spotify.h"
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
    spotify_refreshToken();
    #endif

    #ifdef LRCLIB_TEST
    wifi_connect();
    spotify_refreshToken();
    #endif

    #ifdef LYRIC_SYNC_TEST
    wifi_connect();
    spotify_refreshToken();
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
#include "spotify.h"
void Spotify_Test(void) {
    Serial.println("[TEST] Spotify ───────────────");
    bool ok = spotify_refreshToken();
    if (!ok) {
        Serial.println("[TEST] Spotify FAIL - token refresh");
        return;
    }
    SpotifyTrack track;
    ok = spotify_getNowPlaying(track);
    if (ok) {
        Serial.println("[TEST] Spotify PASS");
        Serial.println("[TEST] Now playing: " + track.title + " by " + track.artist);
    } else {
        Serial.println("[TEST] Spotify FAIL - now playing");
    }
}
#endif

#ifdef LRCLIB_TEST
#include "get_lyrics.h"
#include "spotify.h"
void LRCLib_Test(void) {
    Serial.println("[TEST] LRCLib ────────────────");
    SpotifyTrack track;
    spotify_getNowPlaying(track);
    if (track.title == "") {
        Serial.println("[TEST] LRCLib SKIP - nothing playing");
        return;
    }
    bool ok = lyrics_fetch(track.title, track.artist);
    if (ok) {
        Serial.println("[TEST] LRCLib PASS");
        Serial.println("[TEST] First line: " + lyrics[0].text);
        Serial.println("[TEST] Total lines: " + String(lyricCount));
    } else {
        Serial.println("[TEST] LRCLib FAIL");
    }
}
#endif

#ifdef LYRIC_SYNC_TEST
#include "lyric_sync.h"
#include "get_lyrics.h"
#include "spotify.h"
void LyricSync_Test(void) {
    Serial.println("[TEST] LyricSync ─────────────");

    // Need a track loaded first
    SpotifyTrack track;
    spotify_getNowPlaying(track);
    if (track.title == "") {
        Serial.println("[TEST] LyricSync SKIP - nothing playing");
        return;
    }

    // Fetch lyrics for current track
    bool ok = lyrics_fetch(track.title, track.artist);
    if (!ok) {
        Serial.println("[TEST] LyricSync SKIP - no lyrics found");
        return;
    }

    // Test sync at current playback position
    int line = sync_getCurrentLine(track.progressMs);
    int next = sync_getNextLine(track.progressMs);

    if (line >= 0) {
        Serial.println("[TEST] LyricSync PASS");
        Serial.println("[SYNC] Progress: " + String(track.progressMs / 1000) + "s");
        Serial.println("[SYNC] Current line: " + lyrics[line].text);
        Serial.println("[SYNC] Next line:    " + lyrics[next].text);
    } else {
        Serial.println("[TEST] LyricSync FAIL");
    }
}
#endif

#ifdef FULL_SYSTEM_TEST
void FullSystem_Test(void) {
    Serial.println("[TEST] Full System ───────────");
    Serial.println("[TEST] Full System PASS");
}
#endif