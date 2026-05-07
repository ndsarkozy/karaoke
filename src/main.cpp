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
static volatile unsigned long lastProgressAt = 0;   // millis() of last progress notify
static volatile bool          isPlaying      = false;
static volatile bool          newTrackFlag   = false;

// Phone may delay sending the pause notification by several seconds (Spotify SDK
// callback latency). If we think we're playing but no progress notify has arrived
// recently, treat as paused. Phone now polls every 250 ms while playing, so 700 ms
// gives ~2 missed beats of grace before freezing — quick enough that pauses feel
// instant, with enough margin to absorb normal BLE jitter.
#define PAUSE_INFER_MS 700

// Phone now re-stamps progress to "now" before sending, so the only latency
// left to compensate is the BLE radio hop itself (one connection event at
// 7.5–15 ms, plus stack overhead). 50 ms is a safe over-estimate.
#define BLE_LATENCY_COMP_MS 50

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
        unsigned long now = millis();
        long  newRaw     = ble_getProgressMs() + BLE_LATENCY_COMP_MS;
        long  projected  = progressAnchor + (long)(now - milliAnchor);
        long  delta      = newRaw - projected;
        bool  newPlaying = ble_getIsPlaying();
        bool  playToggled = (newPlaying != isPlaying);

        // Big delta (>1.5 s) or a play/pause toggle = real seek/skip/state
        // change — snap to the new anchor. Small deltas are BLE jitter; ease
        // into them so the highlight doesn't jump back across a line.
        if (abs(delta) > 1500 || playToggled) {
            progressAnchor = newRaw;
        } else if (delta >= 0) {
            // We were running behind — catch up half-way each notify.
            progressAnchor = projected + delta / 2;
        } else {
            // We were running ahead — pull back only 10% per notify so a
            // brief backward correction doesn't drop us across a line.
            progressAnchor = projected + delta / 10;
        }
        milliAnchor    = now;
        lastProgressAt = now;
        durationAnchor = ble_getDurationMs();
        isPlaying      = newPlaying;
    }

    bool effectivelyPlaying = isPlaying &&
        (millis() - lastProgressAt) < PAUSE_INFER_MS;

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
    if (albumReceivedAt > 0 && millis() - albumReceivedAt > 1500) {
        albumReceivedAt = 0;
        Serial.println("[Lyrics] Timeout — no lyrics received after album");
        display_showLyrics("", "", 0, true);
    }

    // ── Lyric sync ────────────────────────────────────────────────────────────
    if (lyricCount == 0) { delay(20); return; }

    long estimated = effectivelyPlaying
        ? progressAnchor + (long)(millis() - milliAnchor)
        : progressAnchor;

    int lineIndices[2];
    sync_getDisplayLines(estimated, lineIndices, 2);
    int currentLine = lineIndices[0];
    if (currentLine < 0) { delay(20); return; }

    // Monotonic line lock: BLE notify jitter or anchor smoothing can leave
    // `estimated` a couple hundred ms behind a line boundary we just crossed.
    // Don't let the displayed line slip back by 1 — but allow real backward
    // seeks (≥2 lines back) to take effect.
    if (lastLine >= 0 && currentLine == lastLine - 1) {
        currentLine = lastLine;
    }

    int highlightWord = sync_getCurrentWord(estimated, currentLine);
    if (highlightWord < 0) {
        // Per-word fallback: weight each word by character count so longer
        // words consume proportionally more of the line's duration. This
        // tracks natural singing far better than dividing time evenly —
        // a 1-letter "I" no longer eats the same slot as "burning".
        const char* text = lyrics[currentLine].text;
        long lineStart = lyrics[currentLine].timestampMs;
        long lineEnd   = (currentLine + 1 < lyricCount)
            ? lyrics[currentLine+1].timestampMs : lineStart + 4000;
        long elapsed  = constrain(estimated - lineStart, 0L,
                                  max(lineEnd - lineStart, 1L));
        long duration = max(lineEnd - lineStart, 1L);

        int totalChars = 0;
        for (int i = 0; i < (int)strlen(text); i++)
            if (text[i] != ' ') totalChars++;
        if (totalChars == 0) totalChars = 1;

        long target = (long)((float)elapsed / duration * totalChars);
        int  acc = 0, wc = 0, cur = 0;
        bool inWord = false;
        for (int i = 0; i < (int)strlen(text); i++) {
            if (text[i] == ' ') { inWord = false; continue; }
            if (!inWord) { inWord = true; wc++; }
            acc++;
            if (acc <= target) cur = wc - 1;
        }
        highlightWord = constrain(cur, 0, max(wc - 1, 0));
    }

    bool lineChanged = (currentLine  != lastLine);

    // Lock highlight monotonic per line: BLE notify jitter can briefly pull
    // `estimated` backward, which would make the active word flicker back.
    if (!lineChanged && lastHighlightWord >= 0 && highlightWord < lastHighlightWord) {
        highlightWord = lastHighlightWord;
    }

    bool wordChanged = (highlightWord != lastHighlightWord);

    if (lineChanged) lastLine = currentLine;
    if (lineChanged || wordChanged) {
        lastHighlightWord = highlightWord;
        const char* nextText = (lineIndices[1] >= 0) ? lyrics[lineIndices[1]].text : "";
        display_showLyrics(lyrics[currentLine].text, nextText, highlightWord, true);
    }

    delay(10);
}
