#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "display.h"
#include "get_lyrics.h"
#include "lyric_sync.h"
#include "ble_client.h"
#include "module_test.h"

static volatile long          progressAnchor = 0;
static volatile long          durationAnchor = 0;
static volatile unsigned long milliAnchor    = 0;
static volatile bool          isPlaying      = false;
static volatile bool          newTrackFlag   = false;

static int           lastLine            = -1;
static int           lastHighlightWord   = -1;
static unsigned long albumReceivedAt     = 0;   // millis() when last album arrived; 0 = no pending timeout

void setup() {
    Serial.begin(115200);
    delay(500);

#ifdef DISPLAY_TEST
    Module_Test_Init();
    return;
#endif

    display_init();
    display_showMessage("Waiting for", "phone...", COLOR_WHITE);
    xTaskCreatePinnedToCore(ble_task, "ble", 16384, NULL, 1, NULL, 0);
}

void loop() {
#ifdef DISPLAY_TEST
    Module_Test_Run();
    return;
#endif

    if (!ble_isConnected()) {
        display_showMessage("Scanning...", "open app", COLOR_WHITE);
        delay(500);
        return;
    }

    // ── New album art ─────────────────────────────────────────────────────────
    if (ble_newAlbumAvailable()) {
        lyrics_clear();
        lastLine          = -1;
        lastHighlightWord = -1;
        albumReceivedAt   = millis();
        // Lock so the BLE callback can't overwrite albumBuf while we decode.
        ble_lockAlbum();
        display_drawAlbum(ble_getAlbumBuf(), ble_getAlbumLen());
        ble_unlockAlbum();
        display_showTrackInfo(trackTitle, trackArtist);
    }

    // ── New lyrics ────────────────────────────────────────────────────────────
    if (ble_newLyricsAvailable()) {
        albumReceivedAt = 0;
        lyrics_parse_ble(ble_getLyrics());  // const char* — no heap copy
        display_showTrackInfo(trackTitle, trackArtist);
        newTrackFlag = true;
    }

    // ── Progress update ───────────────────────────────────────────────────────
    if (ble_newProgressAvailable()) {
        progressAnchor = ble_getProgressMs() + 250;
        durationAnchor = ble_getDurationMs();
        milliAnchor    = millis();
        isPlaying      = ble_getIsPlaying();
    }

    if (newTrackFlag) {
        lastLine          = -1;
        lastHighlightWord = -1;
        newTrackFlag      = false;
        if (lyricCount == 0) {
            display_showLyrics("", "", 0, true);
            delay(20);
            return;
        }
    }

    // ── No-lyrics timeout: show indicator if album arrived but lyrics never came ─
    if (albumReceivedAt > 0 && millis() - albumReceivedAt > 5000) {
        albumReceivedAt = 0;
        Serial.println("[Lyrics] Timeout — no lyrics received after album");
        display_showLyrics("", "", 0, true);
    }

    // ── Lyric sync ────────────────────────────────────────────────────────────
    if (lyricCount == 0) { delay(50); return; }

    long estimated = isPlaying
        ? progressAnchor + (long)(millis() - milliAnchor)
        : progressAnchor;

    int lineIndices[2];
    sync_getDisplayLines(estimated, lineIndices, 2);
    int currentLine = lineIndices[0];
    if (currentLine < 0) { delay(50); return; }

    int highlightWord = sync_getCurrentWord(estimated, currentLine);
    if (highlightWord < 0) {
        long lineStart = lyrics[currentLine].timestampMs;
        long lineEnd   = (currentLine + 1 < lyricCount)
            ? lyrics[currentLine+1].timestampMs : lineStart + 4000;
        long elapsed  = estimated - lineStart;
        long duration = max(lineEnd - lineStart, 1L);
        int  wc = 1;
        for (int i = 0; i < (int)lyrics[currentLine].text.length(); i++)
            if (lyrics[currentLine].text[i] == ' ') wc++;
        highlightWord = constrain((int)((float)elapsed / duration * wc), 0, wc-1);
    }

    bool lineChanged = (currentLine  != lastLine);
    bool wordChanged = (highlightWord != lastHighlightWord);

    if (lineChanged) lastLine = currentLine;
    if (lineChanged || wordChanged) {
        lastHighlightWord = highlightWord;
        String nextText = (lineIndices[1] >= 0) ? lyrics[lineIndices[1]].text : "";
        display_showLyrics(lyrics[currentLine].text, nextText, highlightWord, lineChanged);
    }

    delay(20);
}
