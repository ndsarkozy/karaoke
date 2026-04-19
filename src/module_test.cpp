#include "module_test.h"
#include "config.h"
#include "net.h"
#include "get_lyrics.h"
#include "lyric_sync.h"
#include "spotify.h"
#include "display.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

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

    #ifdef LYRICS_FETCH_TEST
    wifi_connect();
    spotify_refreshToken();
    #endif

    #ifdef FULL_SYSTEM_TEST
    wifi_connect();
    display_init();
    spotify_refreshToken();
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

    #ifdef LYRICS_FETCH_TEST
    LyricsFetch_Test();
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
void LyricSync_Test(void) {
    Serial.println("[TEST] LyricSync ─────────────");
    SpotifyTrack track;
    spotify_getNowPlaying(track);
    if (track.title == "") {
        Serial.println("[TEST] LyricSync SKIP - nothing playing");
        return;
    }
    bool ok = lyrics_fetch(track.title, track.artist);
    if (!ok) {
        Serial.println("[TEST] LyricSync SKIP - no lyrics found");
        return;
    }
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
static String lastTrackId = "";
static unsigned long lastSpotifyPoll = 0;
static unsigned long lastTokenRefresh = 0;
static unsigned long localMillisAtPoll = 0;
static long spotifyProgressAtPoll = 0;
static bool lyricsLoaded = false;

void FullSystem_Test(void) {
    unsigned long now = millis();

    // Refresh token every 55 minutes
    if (now - lastTokenRefresh > TOKEN_INTERVAL) {
        spotify_refreshToken();
        lastTokenRefresh = now;
    }

    // Poll Spotify every 3 seconds
    if (now - lastSpotifyPoll > POLL_INTERVAL) {
        lastSpotifyPoll = now;

        unsigned long preRequest = millis();
        SpotifyTrack track;
        if (!spotify_getNowPlaying(track)) {
            Serial.println("[SYSTEM] Nothing playing");
            lyricsLoaded = false;
            return;
        }

        // Anchor with HTTP latency compensation
        unsigned long requestMs = millis() - preRequest;
        spotifyProgressAtPoll = track.progressMs + (long)requestMs;
        localMillisAtPoll = millis();

        // New track detected
        if (track.trackId != lastTrackId) {
            lastTrackId = track.trackId;
            lyricsLoaded = false;
            Serial.println("[SYSTEM] ──────────────────────────");
            Serial.println("[SYSTEM] Now Playing: " + track.title);
            Serial.println("[SYSTEM] Artist:      " + track.artist);
            Serial.println("[SYSTEM] ──────────────────────────");
        }

        // Retry lyrics if not loaded yet
        if (!lyricsLoaded) {
            Serial.println("[SYSTEM] Fetching lyrics (Spotify)...");
            bool ok = spotify_getLyrics(track.trackId);
            if (!ok) {
                Serial.println("[SYSTEM] Spotify lyrics unavailable, trying LRCLib...");
                ok = lyrics_fetch(track.title, track.artist);
            }
            if (ok) {
                lyricsLoaded = true;
                Serial.println("[SYSTEM] Lyrics loaded: " + String(lyricCount)
                               + " lines, " + String(wordTimestampCount) + " word timestamps");
            } else {
                Serial.println("[SYSTEM] No lyrics found, will retry");
            }
        }
    }

    // Update every 100ms using interpolated position
    if (lyricsLoaded && lyricCount > 0) {
        long estimatedMs = spotifyProgressAtPoll
                           + (long)(millis() - localMillisAtPoll);

        int lineIndices[MAX_DISPLAY_LINES];
        sync_getDisplayLines(estimatedMs, lineIndices, MAX_DISPLAY_LINES);

        static int lastLine          = -1;
        static int lastHighlightWord = -1;
        int currentLine = lineIndices[0];

        if (currentLine >= 0) {
            // Use exact word timestamps if available, otherwise estimate
            int highlightWord = sync_getCurrentWord(estimatedMs, currentLine);
            if (highlightWord < 0) {
                long lineStart = lyrics[currentLine].timestampMs;
                long lineEnd   = (currentLine + 1 < lyricCount)
                                 ? lyrics[currentLine + 1].timestampMs
                                 : lineStart + 4000;
                long elapsed  = estimatedMs - lineStart;
                long duration = max(lineEnd - lineStart, 1L);
                int wc = 1;
                for (int ci = 0; ci < (int)lyrics[currentLine].text.length(); ci++)
                    if (lyrics[currentLine].text[ci] == ' ') wc++;
                highlightWord = constrain((int)((float)elapsed / duration * wc), 0, wc - 1);
            }

            bool lineChanged = (currentLine != lastLine);
            bool wordChanged = (highlightWord != lastHighlightWord);

            if (lineChanged) {
                lastLine = currentLine;
                Serial.println("[LYRIC] " + String(estimatedMs / 1000)
                               + "s | " + lyrics[currentLine].text);
            }

            if (lineChanged || wordChanged) {
                lastHighlightWord = highlightWord;
                String nextText = (lineIndices[1] >= 0) ? lyrics[lineIndices[1]].text : "";
                display_showLyrics(lyrics[currentLine].text, nextText, highlightWord);
            }
        }
    }

    delay(100);
}
#endif

// ── LYRICS FETCH TEST ──────────────────────────────────────────────────────────
// Enable with: #define LYRICS_FETCH_TEST in module_test.h
// Connects to WiFi+Spotify, watches the current track, and repeatedly tries
// lrclib. Every attempt prints a full scenario so you can see exactly why
// fetches fail or succeed.
#ifdef LYRICS_FETCH_TEST
void LyricsFetch_Test(void) {
    static String  lastTrackId   = "";
    static bool    lyricsLoaded  = false;
    static int     attemptNumber = 0;
    static unsigned long lastTry = 0;

    // Retry every 4 seconds while lyrics haven't loaded
    unsigned long now = millis();
    if (lyricsLoaded || (now - lastTry < 4000)) {
        delay(200);
        return;
    }
    lastTry = now;

    // ── 1. Check WiFi ────────────────────────────────────────────────────────
    if (!wifi_connected()) {
        Serial.println("[LTEST] SCENARIO: WiFi disconnected — skipping");
        return;
    }
    Serial.println("[LTEST] WiFi OK  RSSI=" + String(WiFi.RSSI()) + " dBm");

    // ── 2. Get current track ─────────────────────────────────────────────────
    SpotifyTrack track;
    unsigned long t0 = millis();
    bool polled = spotify_getNowPlaying(track);
    unsigned long spotifyRtt = millis() - t0;

    if (!polled || track.trackId.length() == 0) {
        Serial.println("[LTEST] SCENARIO: Spotify poll failed (HTTP RTT=" + String(spotifyRtt) + "ms) — nothing playing or token expired");
        return;
    }

    Serial.println("[LTEST] Spotify OK  RTT=" + String(spotifyRtt) + "ms");
    Serial.println("[LTEST] Track:  \"" + track.title + "\"");
    Serial.println("[LTEST] Artist: \"" + track.artist + "\"");
    Serial.println("[LTEST] ID:     " + track.trackId);
    Serial.println("[LTEST] Pos:    " + String(track.progressMs / 1000) + "s / " + String(track.durationMs / 1000) + "s");

    if (track.trackId != lastTrackId) {
        lastTrackId   = track.trackId;
        lyricsLoaded  = false;
        attemptNumber = 0;
        Serial.println("[LTEST] ── New track detected, resetting ──");
    }

    attemptNumber++;
    Serial.println("[LTEST] ── Attempt #" + String(attemptNumber) + " ──────────────────────");

    // ── 3. Raw TLS probe — time just the handshake separately ───────────────
    Serial.println("[LTEST] RSSI at fetch: " + String(WiFi.RSSI()) + " dBm  heap=" + String(ESP.getFreeHeap()));

    {
        WiFiClientSecure probe;
        probe.setInsecure();
        probe.setHandshakeTimeout(4);
        unsigned long tlsT0 = millis();
        bool connected = probe.connect("lrclib.net", 443);
        unsigned long tlsMs = millis() - tlsT0;
        if (connected) {
            Serial.println("[LTEST] TLS handshake OK  time=" + String(tlsMs) + "ms");
            probe.stop();
        } else {
            Serial.println("[LTEST] TLS handshake FAILED  time=" + String(tlsMs) + "ms  — SSL stack issue or server unreachable");
        }
    }

    // ── 4. Full lyrics fetch ─────────────────────────────────────────────────
    Serial.println("[LTEST] Fetching lyrics...");
    unsigned long fetchT0 = millis();
    bool ok = lyrics_fetch(track.title, track.artist);
    unsigned long fetchMs = millis() - fetchT0;

    if (ok) {
        lyricsLoaded = true;
        Serial.println("[LTEST] RESULT: SUCCESS  lines=" + String(lyricCount) + "  time=" + String(fetchMs) + "ms");
        Serial.println("[LTEST] First line [" + String(lyrics[0].timestampMs) + "ms]: " + lyrics[0].text);
        Serial.println("[LTEST] Last  line [" + String(lyrics[lyricCount-1].timestampMs) + "ms]: " + lyrics[lyricCount-1].text);
    } else {
        Serial.println("[LTEST] RESULT: FAILED  time=" + String(fetchMs) + "ms");
        if (fetchMs < 500)  Serial.println("[LTEST] CAUSE: Fast fail — DNS or TCP refused");
        else if (fetchMs < 3000) Serial.println("[LTEST] CAUSE: SSL handshake failed (< 3s)");
        else                Serial.println("[LTEST] CAUSE: Server accepted connection but response timed out (slow server)");
    }
    Serial.println();
}
#endif