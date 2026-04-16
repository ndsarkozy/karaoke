#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "config.h"
#include "display.h"
#include "spotify.h"
#include "get_lyrics.h"
#include "lyric_sync.h"
#include "net.h"

// ── Shared state ──────────────────────────────────────────────────────────────
static SemaphoreHandle_t xMutex;

static volatile bool          lyricsReady    = false;
static volatile bool          newTrackFlag   = false;
static volatile bool          artReady       = false;
static volatile bool          isPlaying      = false;
static volatile long          progressAnchor = 0;
static volatile unsigned long milliAnchor    = 0;
static String                 trackTitle     = "";
static String                 trackArtist    = "";
static uint8_t*               artBuffer      = nullptr;
static size_t                 artBufferSize  = 0;

// ── Download raw bytes from URL into heap buffer ──────────────────────────────
static uint8_t* fetchBuffer(const String &url, size_t &outLen) {
    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();
    http.begin(client, url);
    http.setTimeout(8000);

    if (http.GET() != 200) { http.end(); return nullptr; }

    int contentLen = http.getSize();
    if (contentLen > 40000) { http.end(); return nullptr; }

    size_t bufSize = (contentLen > 0) ? (size_t)contentLen : 40000;
    uint8_t* buf = (uint8_t*)malloc(bufSize);
    if (!buf) { http.end(); return nullptr; }

    WiFiClient* stream = http.getStreamPtr();
    size_t total = 0;
    unsigned long deadline = millis() + 8000;

    while (millis() < deadline && total < bufSize) {
        int avail = stream->available();
        if (avail > 0) {
            total += stream->readBytes(buf + total, min((int)(bufSize - total), avail));
        } else if (!stream->connected()) {
            break;
        } else {
            delay(1);
        }
    }

    http.end();
    if (total == 0) { free(buf); return nullptr; }
    outLen = total;
    return buf;
}

// ── Network task — core 0 ─────────────────────────────────────────────────────
static void networkTask(void *pv) {
    unsigned long lastPoll         = 0;
    unsigned long lastTokenRefresh = 0;
    String        lastTrackId      = "";
    bool          lastIsPlaying    = true;

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
            if (!spotify_getNowPlaying(track) || track.trackId.length() == 0) {
                lastPoll = 0;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            unsigned long rtt = millis() - t0;

            // ── Detect pause / resume ─────────────────────────────────────
            bool pauseChanged = (track.isPlaying != lastIsPlaying);
            if (pauseChanged) {
                lastIsPlaying = track.isPlaying;
                Serial.println(track.isPlaying ? "[NET] Resumed" : "[NET] Paused");
                lastPoll = 0;  // re-poll immediately to lock in the new state
            }

            // ── Detect seek (only while actively playing) ─────────────────
            xSemaphoreTake(xMutex, portMAX_DELAY);
            long expectedMs = isPlaying
                              ? progressAnchor + (long)(millis() - milliAnchor)
                              : progressAnchor;
            xSemaphoreGive(xMutex);

            long drift = abs(track.progressMs - expectedMs);
            bool seekDetected = track.isPlaying && !pauseChanged && (drift > 2000);
            if (seekDetected) {
                Serial.println("[NET] Seek detected, drift=" + String(drift) + "ms");
                lastPoll = 0;  // re-poll to confirm new position
            }

            // ── Update anchor — always use fresh Spotify data ─────────────
            // Half RTT is the best estimate of one-way latency
            xSemaphoreTake(xMutex, portMAX_DELAY);
            progressAnchor = track.isPlaying
                             ? track.progressMs + (long)(rtt / 2)
                             : track.progressMs;
            milliAnchor    = millis();
            isPlaying      = track.isPlaying;
            xSemaphoreGive(xMutex);

            // ── New track ─────────────────────────────────────────────────
            if (track.trackId != lastTrackId) {
                lastTrackId = track.trackId;
                Serial.println("[NET] New track: " + track.title);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                trackTitle   = track.title;
                trackArtist  = track.artist;
                newTrackFlag = true;
                lyricsReady  = false;
                artReady     = false;
                if (artBuffer) { free(artBuffer); artBuffer = nullptr; }
                xSemaphoreGive(xMutex);

                // Album art
                if (track.albumArtUrl.length() > 0) {
                    size_t artLen = 0;
                    uint8_t* art = fetchBuffer(track.albumArtUrl, artLen);
                    xSemaphoreTake(xMutex, portMAX_DELAY);
                    artBuffer     = art;
                    artBufferSize = artLen;
                    artReady      = (art != nullptr);
                    xSemaphoreGive(xMutex);
                    Serial.println("[NET] Art: " + String(artBufferSize) + " bytes");
                }

                // Lyrics
                bool ok = spotify_getLyrics(track.trackId);
                if (!ok) ok = lyrics_fetch(track.title, track.artist);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                lyricsReady = ok;
                xSemaphoreGive(xMutex);

                Serial.println("[NET] Lyrics: " + String(lyricCount)
                               + " lines, " + String(wordTimestampCount) + " words");

                lastPoll = 0;  // catch rapid song switches immediately
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
    // Atomic snapshot of all shared state
    xSemaphoreTake(xMutex, portMAX_DELAY);
    bool          ready    = lyricsReady;
    bool          newTrack = newTrackFlag;
    bool          hasArt   = artReady;
    bool          playing  = isPlaying;
    long          anchor   = progressAnchor;
    unsigned long mAnchor  = milliAnchor;
    String        title    = trackTitle;
    String        artist   = trackArtist;
    uint8_t*      artBuf   = nullptr;
    size_t        artSz    = 0;
    if (newTrack) newTrackFlag = false;
    if (hasArt) {
        artBuf    = artBuffer;
        artSz     = artBufferSize;
        artBuffer = nullptr;
        artReady  = false;
    }
    xSemaphoreGive(xMutex);

    // New track: show card immediately while lyrics + art load
    if (newTrack) {
        display_showTrack(title, artist);
        showingTrack      = true;
        lastLine          = -1;
        lastHighlightWord = -1;
    }

    // Album art arrived: overlay on track card
    if (hasArt && artBuf && showingTrack) {
        display_drawJpeg(artBuf, artSz, 45, 7);
        free(artBuf);
        display_showTrackText(title, artist);
    } else if (hasArt && artBuf) {
        free(artBuf);
    }

    if (!ready || lyricCount == 0) {
        delay(50);
        return;
    }

    // Freeze progress when paused; advance with millis() when playing
    long estimated = playing
                     ? anchor + (long)(millis() - mAnchor)
                     : anchor;

    int lineIndices[2];
    sync_getDisplayLines(estimated, lineIndices, 2);
    int currentLine = lineIndices[0];

    if (currentLine < 0) { delay(50); return; }

    // Exact word timestamps if available, otherwise even distribution
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
