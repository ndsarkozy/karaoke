#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "config.h"
#include "display.h"
#include "spotify.h"
#include "get_lyrics.h"
#include "lyric_sync.h"
#include "net.h"

// ── Shared state (network task writes, display loop reads) ────────────────────
static SemaphoreHandle_t xMutex;

static volatile bool         lyricsReady    = false;
static volatile bool         newTrackFlag   = false;
static volatile long         progressAnchor = 0;
static volatile unsigned long milliAnchor   = 0;
static String                trackTitle     = "";
static String                trackArtist    = "";

// ── Network task — core 0 ─────────────────────────────────────────────────────
static void networkTask(void *pv) {
    unsigned long lastPoll         = 0;
    unsigned long lastTokenRefresh = 0;
    String        lastTrackId      = "";

    while (true) {
        unsigned long now = millis();

        if (now - lastTokenRefresh > TOKEN_INTERVAL) {
            spotify_refreshToken();
            lastTokenRefresh = now;
        }

        if (now - lastPoll >= POLL_INTERVAL) {
            lastPoll = now;

            unsigned long t0 = millis();
            SpotifyTrack track;
            if (!spotify_getNowPlaying(track)) {
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            // Update interpolation anchor with HTTP latency compensation
            xSemaphoreTake(xMutex, portMAX_DELAY);
            progressAnchor = track.progressMs + (long)(millis() - t0);
            milliAnchor    = millis();
            xSemaphoreGive(xMutex);

            if (track.trackId != lastTrackId) {
                lastTrackId = track.trackId;
                Serial.println("[NET] New track: " + track.title);

                // Signal display loop before blocking on lyrics fetch
                xSemaphoreTake(xMutex, portMAX_DELAY);
                trackTitle   = track.title;
                trackArtist  = track.artist;
                newTrackFlag = true;
                lyricsReady  = false;
                xSemaphoreGive(xMutex);

                // Fetch lyrics — blocks here, display loop stays responsive
                bool ok = spotify_getLyrics(track.trackId);
                if (!ok) ok = lyrics_fetch(track.title, track.artist);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                lyricsReady = ok;
                xSemaphoreGive(xMutex);

                Serial.println("[NET] Lyrics: " + String(lyricCount)
                               + " lines, " + String(wordTimestampCount) + " words");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== Karaoke ESP32 Boot ===");

    xMutex = xSemaphoreCreateMutex();

    display_init();
    display_showMessage("Connecting...", "", COLOR_WHITE);
    wifi_connect();

    display_showMessage("Authorizing...", "", COLOR_WHITE);
    spotify_refreshToken();

    display_showMessage("Ready", "", COLOR_GREEN);
    delay(800);

    xTaskCreatePinnedToCore(networkTask, "net", 10240, NULL, 1, NULL, 0);
}

// ── Display loop — core 1 ─────────────────────────────────────────────────────
static int  lastLine          = -1;
static int  lastHighlightWord = -1;
static bool showingTrack      = false;

void loop() {
    // Snapshot shared state atomically
    xSemaphoreTake(xMutex, portMAX_DELAY);
    bool          ready    = lyricsReady;
    bool          newTrack = newTrackFlag;
    long          anchor   = progressAnchor;
    unsigned long mAnchor  = milliAnchor;
    String        title    = trackTitle;
    String        artist   = trackArtist;
    if (newTrack) newTrackFlag = false;
    xSemaphoreGive(xMutex);

    // Show track card immediately while lyrics are loading
    if (newTrack) {
        display_showTrack(title, artist);
        showingTrack      = true;
        lastLine          = -1;
        lastHighlightWord = -1;
    }

    if (!ready || lyricCount == 0) {
        delay(50);
        return;
    }

    long estimated = anchor + (long)(millis() - mAnchor);

    int lineIndices[2];
    sync_getDisplayLines(estimated, lineIndices, 2);
    int currentLine = lineIndices[0];

    if (currentLine < 0) { delay(50); return; }

    // Exact word timestamp if available, otherwise evenly distribute
    int highlightWord = sync_getCurrentWord(estimated, currentLine);
    if (highlightWord < 0) {
        long lineStart = lyrics[currentLine].timestampMs;
        long lineEnd   = (currentLine + 1 < lyricCount)
                         ? lyrics[currentLine + 1].timestampMs
                         : lineStart + 4000;
        long elapsed  = estimated - lineStart;
        long duration = max(lineEnd - lineStart, 1L);
        int wc = 1;
        for (int ci = 0; ci < (int)lyrics[currentLine].text.length(); ci++)
            if (lyrics[currentLine].text[ci] == ' ') wc++;
        highlightWord = constrain((int)((float)elapsed / duration * wc), 0, wc - 1);
    }

    bool lineChanged = (currentLine != lastLine);
    bool wordChanged = (highlightWord != lastHighlightWord);

    if (lineChanged) {
        if (showingTrack) {
            display_clear();
            showingTrack = false;
        }
        lastLine = currentLine;
        Serial.println("[LYRIC] " + lyrics[currentLine].text);
    }

    if (lineChanged || wordChanged) {
        lastHighlightWord = highlightWord;
        String nextText = (lineIndices[1] >= 0) ? lyrics[lineIndices[1]].text : "";
        display_showLyrics(lyrics[currentLine].text, nextText, highlightWord);
    }

    delay(50);
}
