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

// ── Download raw bytes from URL into heap buffer ─────────────────────────────
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

// ── Network task ──────────────────────────────────────────────────────────────
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

            // ── Pause / resume detection ────────────────────────────────
            bool pauseChanged = (track.isPlaying != lastIsPlaying);
            if (pauseChanged) {
                lastIsPlaying = track.isPlaying;
                Serial.println(track.isPlaying ? "[NET] Resumed" : "[NET] Paused");
                lastPoll = 0;

                xSemaphoreTake(xMutex, portMAX_DELAY);
                milliAnchor = millis();   // reset ONLY here
                xSemaphoreGive(xMutex);
            }

            // ── Drift detection (seek detection) ────────────────────────
            xSemaphoreTake(xMutex, portMAX_DELAY);
            long expected = progressAnchor + (long)(millis() - milliAnchor);
            xSemaphoreGive(xMutex);

            long drift = abs(track.progressMs - expected);

            bool seekDetected = track.isPlaying && !pauseChanged && (drift > 3500);
            if (seekDetected) {
                Serial.println("[NET] Seek detected drift=" + String(drift));
                lastPoll = 0;
            }

            // ── Anchor correction (FIXED smoothing model) ───────────────
            xSemaphoreTake(xMutex, portMAX_DELAY);

            isPlaying = track.isPlaying;

            long local = progressAnchor + (long)(millis() - milliAnchor);
            float error = (float)(track.progressMs - local);

            // stable correction (no accumulation bug)
            const float alpha = 0.12;
            progressAnchor += (long)(alpha * error);

            xSemaphoreGive(xMutex);

            // ── New track ───────────────────────────────────────────────
            if (track.trackId != lastTrackId) {
                lastTrackId = track.trackId;
                Serial.println("[NET] New track: " + track.title);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                trackTitle   = track.title;
                trackArtist  = track.artist;
                newTrackFlag = true;
                lyricsReady  = false;
                artReady     = false;
                milliAnchor  = millis();   // reset ONLY on track change

                if (artBuffer) {
                    free(artBuffer);
                    artBuffer = nullptr;
                }
                xSemaphoreGive(xMutex);

                // album art
                if (track.albumArtUrl.length() > 0) {
                    size_t artLen = 0;
                    uint8_t* art = fetchBuffer(track.albumArtUrl, artLen);

                    xSemaphoreTake(xMutex, portMAX_DELAY);
                    artBuffer     = art;
                    artBufferSize = artLen;
                    artReady      = (art != nullptr);
                    xSemaphoreGive(xMutex);
                }

                // lyrics
                bool ok = spotify_getLyrics(track.trackId);
                if (!ok) ok = lyrics_fetch(track.title, track.artist);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                lyricsReady = ok;
                xSemaphoreGive(xMutex);

                lastPoll = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    xMutex = xSemaphoreCreateMutex();

    display_init();
    display_showMessage("Connecting...", "", COLOR_WHITE);

    wifi_connect();

    display_showMessage("Authorizing...", "", COLOR_WHITE);
    spotify_refreshToken();

    display_showMessage("Ready", "", COLOR_GREEN);

    xTaskCreatePinnedToCore(networkTask, "net", 10240, NULL, 1, NULL, 0);
}

// ── Display loop ──────────────────────────────────────────────────────────────
static int  lastLine = -1;
static int  lastHighlightWord = -1;
static bool showingTrack = false;

void loop() {
    xSemaphoreTake(xMutex, portMAX_DELAY);

    bool ready    = lyricsReady;
    bool newTrack = newTrackFlag;
    bool hasArt   = artReady;
    bool playing  = isPlaying;

    long anchor   = progressAnchor;
    unsigned long mAnchor = milliAnchor;

    String title  = trackTitle;
    String artist = trackArtist;

    uint8_t* artBuf = nullptr;
    size_t artSz = 0;

    if (newTrack) newTrackFlag = false;

    if (hasArt) {
        artBuf = artBuffer;
        artSz  = artBufferSize;
        artBuffer = nullptr;
        artReady = false;
    }

    xSemaphoreGive(xMutex);

    if (newTrack) {
        display_showTrack(title, artist);
        showingTrack = true;
        lastLine = -1;
        lastHighlightWord = -1;
    }

    if (hasArt && artBuf && showingTrack) {
        display_drawJpeg(artBuf, artSz, -30, -30);
        free(artBuf);
        display_showTrackText(title, artist);
    } else if (hasArt && artBuf) {
        free(artBuf);
    }

    if (!ready || lyricCount == 0) {
        delay(50);
        return;
    }

    long estimated = playing
        ? anchor + (long)(millis() - mAnchor)
        : anchor;

    int lineIndices[2];
    sync_getDisplayLines(estimated, lineIndices, 2);

    int currentLine = lineIndices[0];
    if (currentLine < 0) { delay(50); return; }

    int highlightWord = sync_getCurrentWord(estimated, currentLine);

    if (highlightWord < 0) {
        long lineStart = lyrics[currentLine].timestampMs;
        long lineEnd = (currentLine + 1 < lyricCount)
            ? lyrics[currentLine + 1].timestampMs
            : lineStart + 4000;

        long elapsed = estimated - lineStart;
        long duration = max(lineEnd - lineStart, 1L);

        int wc = 1;
        for (int i = 0; i < (int)lyrics[currentLine].text.length(); i++)
            if (lyrics[currentLine].text[i] == ' ') wc++;

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
    }

    if (lineChanged || wordChanged) {
        lastHighlightWord = highlightWord;

        String nextText = (lineIndices[1] >= 0)
            ? lyrics[lineIndices[1]].text
            : "";

        display_showLyrics(
            lyrics[currentLine].text,
            nextText,
            highlightWord
        );
    }

    delay(50);
}