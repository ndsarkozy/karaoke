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
static SemaphoreHandle_t httpMutex;     // only one SSL/HTTP connection at a time
static TaskHandle_t      fetchTaskHandle = NULL;

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

// Staged by pollTask, consumed by fetchTask
static String                 pendingTrackId = "";
static String                 pendingTitle   = "";
static String                 pendingArtist  = "";
static String                 pendingArtUrl  = "";

// ── Download raw bytes from URL into heap buffer ──────────────────────────────
// Caller must hold httpMutex
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

// ── Fetch task: lyrics first, art second ─────────────────────────────────────
static void fetchTask(void *pv) {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        xSemaphoreTake(xMutex, portMAX_DELAY);
        String trackId = pendingTrackId;
        String title   = pendingTitle;
        String artist  = pendingArtist;
        String artUrl  = pendingArtUrl;
        xSemaphoreGive(xMutex);

        Serial.println("[FETCH] Starting: " + title);

        // ── 1. Lyrics first — release httpMutex after so pollTask can poll ──
        xSemaphoreTake(httpMutex, portMAX_DELAY);
        bool ok = spotify_getLyrics(trackId);
        xSemaphoreGive(httpMutex);

        if (!ok) {
            xSemaphoreTake(httpMutex, portMAX_DELAY);
            ok = lyrics_fetch(title, artist);
            xSemaphoreGive(httpMutex);
        }

        xSemaphoreTake(xMutex, portMAX_DELAY);
        lyricsReady = ok;
        xSemaphoreGive(xMutex);

        Serial.println("[FETCH] Lyrics " + String(ok ? "OK" : "FAIL"));

        // ── 2. Art second — httpMutex released before and after ─────────────
        if (artUrl.length() > 0) {
            size_t artLen = 0;
            uint8_t* art = nullptr;

            xSemaphoreTake(httpMutex, portMAX_DELAY);
            art = fetchBuffer(artUrl, artLen);
            xSemaphoreGive(httpMutex);

            xSemaphoreTake(xMutex, portMAX_DELAY);
            if (artBuffer) { free(artBuffer); artBuffer = nullptr; }
            artBuffer     = art;
            artBufferSize = artLen;
            artReady      = (art != nullptr);
            xSemaphoreGive(xMutex);

            Serial.println("[FETCH] Art " + String(art ? "OK" : "FAIL"));
        }
    }
}

// ── Poll task: Spotify state only ─────────────────────────────────────────────
static void pollTask(void *pv) {
    unsigned long lastPoll         = 0;
    unsigned long lastTokenRefresh = 0;
    String        lastTrackId      = "";
    bool          lastIsPlaying    = true;
    int           fastPollCount    = 0;

    while (true) {
        unsigned long now = millis();

        if (now - lastTokenRefresh > TOKEN_INTERVAL) {
            xSemaphoreTake(httpMutex, portMAX_DELAY);
            spotify_refreshToken();
            xSemaphoreGive(httpMutex);
            lastTokenRefresh = now;
        }

        unsigned long pollInterval = (fastPollCount > 0) ? 250 : 1000;

        if (now - lastPoll >= pollInterval) {
            lastPoll = now;
            if (fastPollCount > 0) fastPollCount--;

            // Skip this poll cycle if fetchTask is mid-download
            if (xSemaphoreTake(httpMutex, pdMS_TO_TICKS(150)) != pdTRUE) {
                fastPollCount++;   // retry next cycle at fast rate
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            unsigned long t0 = millis();
            SpotifyTrack track;
            bool polled = spotify_getNowPlaying(track);
            xSemaphoreGive(httpMutex);

            unsigned long rtt    = millis() - t0;
            unsigned long t_recv = millis();

            if (!polled || track.trackId.length() == 0) {
                fastPollCount = 0;
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }

            // ── Pause / resume detection ────────────────────────────────────
            bool pauseChanged = (track.isPlaying != lastIsPlaying);
            if (pauseChanged) {
                lastIsPlaying = track.isPlaying;
                Serial.println(track.isPlaying ? "[POLL] Resumed" : "[POLL] Paused");
                fastPollCount = 12;
            }

            // ── New track — must come BEFORE anchor update ──────────────────
            if (track.trackId != lastTrackId) {
                lastTrackId = track.trackId;
                Serial.println("[POLL] New track: " + track.title);
                fastPollCount = 8;

                // Cap RTT compensation: high RTT during contention is noise
                long rtt_comp = (long)min(rtt / 2, (unsigned long)400);

                xSemaphoreTake(xMutex, portMAX_DELAY);
                trackTitle     = track.title;
                trackArtist    = track.artist;
                newTrackFlag   = true;
                lyricsReady    = false;
                artReady       = false;
                isPlaying      = track.isPlaying;
                progressAnchor = track.progressMs + rtt_comp;
                milliAnchor    = t_recv;

                pendingTrackId = track.trackId;
                pendingTitle   = track.title;
                pendingArtist  = track.artist;
                pendingArtUrl  = track.albumArtUrl;

                if (artBuffer) { free(artBuffer); artBuffer = nullptr; }
                xSemaphoreGive(xMutex);

                if (fetchTaskHandle) xTaskNotifyGive(fetchTaskHandle);

                vTaskDelay(pdMS_TO_TICKS(50));
                continue;  // skip anchor correction on new track
            }

            // ── Anchor correction with RTT compensation ─────────────────────
            // Cap RTT comp so network contention doesn't corrupt the anchor
            long rtt_comp = (long)min(rtt / 2, (unsigned long)400);

            xSemaphoreTake(xMutex, portMAX_DELAY);

            isPlaying = track.isPlaying;

            if (!track.isPlaying) {
                // Paused: snap to exact reported position
                progressAnchor = track.progressMs;
                milliAnchor    = t_recv;
            } else {
                long corrected = track.progressMs + rtt_comp;
                long local     = progressAnchor + (long)(t_recv - milliAnchor);
                long drift     = corrected - local;

                if (abs(drift) > 3000) {
                    // Seek: snap immediately
                    Serial.println("[POLL] Seek snap drift=" + String(drift) + "ms");
                    progressAnchor = corrected;
                    milliAnchor    = t_recv;
                    fastPollCount  = 12;
                } else {
                    // Jitter: converge with alpha (higher = faster convergence)
                    progressAnchor += (long)(0.4f * (float)drift);
                }
            }

            xSemaphoreGive(xMutex);

            // Poll fast near end of track
            if (track.isPlaying && track.durationMs > 0 &&
                track.progressMs > track.durationMs - 2000) {
                fastPollCount = max(fastPollCount, 8);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);

    xMutex    = xSemaphoreCreateMutex();
    httpMutex = xSemaphoreCreateMutex();

    display_init();
    display_showMessage("Connecting...", "", COLOR_WHITE);

    wifi_connect();

    display_showMessage("Authorizing...", "", COLOR_WHITE);
    spotify_refreshToken();

    display_showMessage("Ready", "", COLOR_GREEN);

    // fetchTask created first so handle is valid when pollTask starts
    xTaskCreatePinnedToCore(fetchTask, "fetch", 10240, NULL, 1, &fetchTaskHandle, 0);
    xTaskCreatePinnedToCore(pollTask,  "poll",  8192,  NULL, 2, NULL,             0);
}

// ── Display loop ──────────────────────────────────────────────────────────────
static int  lastLine          = -1;
static int  lastHighlightWord = -1;

void loop() {
    xSemaphoreTake(xMutex, portMAX_DELAY);

    bool ready    = lyricsReady;
    bool newTrack = newTrackFlag;
    bool hasArt   = artReady;
    bool playing  = isPlaying;

    long          anchor  = progressAnchor;
    unsigned long mAnchor = milliAnchor;

    uint8_t* artBuf = nullptr;
    size_t   artSz  = 0;

    if (newTrack) newTrackFlag = false;

    if (hasArt) {
        artBuf    = artBuffer;
        artSz     = artBufferSize;
        artBuffer = nullptr;
        artReady  = false;
    }

    xSemaphoreGive(xMutex);

    if (newTrack) {
        display_clear();
        lastLine          = -1;
        lastHighlightWord = -1;
    }

    if (hasArt && artBuf) {
        display_drawJpeg(artBuf, artSz, -30, -30);
        free(artBuf);
        lastLine          = -1;
        lastHighlightWord = -1;
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
        long lineEnd   = (currentLine + 1 < lyricCount)
            ? lyrics[currentLine + 1].timestampMs
            : lineStart + 4000;

        long elapsed  = estimated - lineStart;
        long duration = max(lineEnd - lineStart, 1L);

        int wc = 1;
        for (int i = 0; i < (int)lyrics[currentLine].text.length(); i++)
            if (lyrics[currentLine].text[i] == ' ') wc++;

        highlightWord = constrain((int)((float)elapsed / duration * wc), 0, wc - 1);
    }

    bool lineChanged = (currentLine != lastLine);
    bool wordChanged = (highlightWord != lastHighlightWord);

    if (lineChanged) lastLine = currentLine;

    if (lineChanged || wordChanged) {
        lastHighlightWord = highlightWord;
        String nextText   = (lineIndices[1] >= 0) ? lyrics[lineIndices[1]].text : "";
        display_showLyrics(lyrics[currentLine].text, nextText, highlightWord);
    }

    delay(50);
}
