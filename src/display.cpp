#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();

// ─── Cached state ────────────────────────────────────────────────────────────
static const uint8_t* s_albumBuf      = nullptr;
static size_t         s_albumLen      = 0;
static bool           s_albumLoaded   = false;
static int            s_lastArcAngle  = -1;
static long           s_lastProgress  = 0;
static long           s_lastDuration  = 0;

static String s_title  = "";
static String s_artist = "";

// ─── Album cache (post-dim) ──────────────────────────────────────────────────
// Captures the *dimmed* decoded album rows once at album-load. On line change
// we pushImage the cached pixels back to wipe old text without re-decoding.
#define ALBUM_CACHE_Y     76
#define ALBUM_CACHE_H     124
static uint16_t* s_albumCache       = nullptr;
static bool      s_albumCacheValid  = false;
static bool      s_capturingAlbum   = false;

// Dim factor for album backdrop (Car Thing aesthetic — art is a textured
// background, not the focus). Halves each RGB565 channel via mask + shift.
//   shift=1 → ~50% brightness
//   shift=2 → ~25% brightness (more dramatic)
#define DIM_HALVE(c)   (((c) >> 1) & 0x7BEF)
#define DIM_QUARTER(c) (((c) >> 2) & 0x39E7)

// ─── JPEG callback: dim each block in place, push, then capture to cache ────
static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bm) {
    int16_t x1 = max(x, (int16_t)0), y1 = max(y, (int16_t)0);
    int16_t x2 = min((int16_t)(x + w), (int16_t)240);
    int16_t y2 = min((int16_t)(y + h), (int16_t)240);
    if (x1 >= x2 || y1 >= y2) return true;

    // Dim every pixel of this block in place. Halve once (50%), then again on
    // top-half of screen to fade title area more — matches Car Thing's heavier
    // dim toward the top where artist/title sit.
    int blockN = w * h;
    for (int i = 0; i < blockN; i++) bm[i] = DIM_HALVE(bm[i]);

    tft.pushImage(x1, y1, x2-x1, y2-y1, bm + (y1-y)*w + (x1-x));

    if (s_capturingAlbum && s_albumCache) {
        int16_t cy1 = max(y1, (int16_t)ALBUM_CACHE_Y);
        int16_t cy2 = min(y2, (int16_t)(ALBUM_CACHE_Y + ALBUM_CACHE_H));
        for (int row = cy1; row < cy2; row++) {
            int srcRow = row - y;
            int dstRow = row - ALBUM_CACHE_Y;
            memcpy(&s_albumCache[dstRow * 240 + x1],
                   bm + srcRow * w + (x1 - x),
                   (x2 - x1) * sizeof(uint16_t));
        }
    }
    return true;
}

// ─── Init ────────────────────────────────────────────────────────────────────
void display_init() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(jpegOutput);
    Serial.printf("[Display] Initialized (free heap=%u)\n",
                  (unsigned)ESP.getFreeHeap());
}

void display_fill(uint16_t c)   { tft.fillScreen(c); }
void display_clear()            { tft.fillScreen(COLOR_BLACK); s_albumLoaded = false; s_lastArcAngle = -1; }
void display_drawCircle(int x, int y, int r, uint16_t c) { tft.drawCircle(x,y,r,c); }
void display_fillCircle(int x, int y, int r, uint16_t c) { tft.fillCircle(x,y,r,c); }
void display_brightness(uint8_t v) { analogWrite(TFT_BL, v); }

void display_print(const String &text, int x, int y, uint16_t color, uint8_t size) {
    tft.setFreeFont(NULL);
    tft.setTextColor(color, COLOR_BLACK);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

// ─── Idle splash ─────────────────────────────────────────────────────────────
void display_showMessage(const String &l1, const String &l2, uint16_t color) {
    display_clear();
    tft.drawCircle(120, 120, 119, COLOR_ACCENT_DIM);
    tft.drawCircle(120, 120, 118, OVERLAY_DARK);

    tft.setTextDatum(MC_DATUM);
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextColor(COLOR_TEXT_HI, COLOR_BLACK);
    tft.drawString(l1, 120, 110);

    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(COLOR_ACCENT, COLOR_BLACK);
    tft.drawString(l2, 120, 138);

    tft.setFreeFont(NULL);
    tft.setTextDatum(TL_DATUM);
}

// ─── Album art ───────────────────────────────────────────────────────────────
bool display_drawAlbum(const uint8_t* buf, size_t len) {
    if (!buf || len == 0) return false;

    if (!s_albumCache) {
        s_albumCache = (uint16_t*)malloc(240 * ALBUM_CACHE_H * sizeof(uint16_t));
        Serial.printf("[Display] Album cache alloc %s (free heap=%u)\n",
                      s_albumCache ? "ok" : "FAIL", (unsigned)ESP.getFreeHeap());
    }

    s_capturingAlbum = (s_albumCache != nullptr);
    JRESULT res = TJpgDec.drawJpg(0, 0, buf, (uint32_t)len);
    s_capturingAlbum = false;
    if (res != JDR_OK) { s_albumCacheValid = false; return false; }
    s_albumBuf        = buf;
    s_albumLen        = len;
    s_albumLoaded     = true;
    s_albumCacheValid = (s_albumCache != nullptr);
    s_lastArcAngle    = -1;
    return true;
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
static String fitText(const String &in, int maxW) {
    if (tft.textWidth(in) <= maxW) return in;
    String t = in;
    while (t.length() > 1 && tft.textWidth(t + "…") > maxW)
        t = t.substring(0, t.length() - 1);
    return t + "…";
}

// Letter-tracked drawString: emulates small-caps tracking (Car Thing artist)
static int trackedWidth(const String &s, int trackPx) {
    int w = 0;
    for (int i = 0; i < (int)s.length(); i++) {
        char cs[2] = { (char)s[i], 0 };
        w += tft.textWidth(cs) + trackPx;
    }
    return w > 0 ? w - trackPx : 0;
}

static void drawTrackedCentered(const String &s, int cx, int y, uint16_t col, int trackPx) {
    int totalW = trackedWidth(s, trackPx);
    int x = cx - totalW / 2;
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(col);   // transparent bg — text floats on dimmed art
    for (int i = 0; i < (int)s.length(); i++) {
        char cs[2] = { (char)s[i], 0 };
        tft.drawString(cs, x, y);
        x += tft.textWidth(cs) + trackPx;
    }
}

// ─── Track info: artist (uppercase tracked) + bold title ─────────────────────
static void renderTrackInfo() {
    if (s_title.length() == 0 && s_artist.length() == 0) return;

    // Artist — UPPERCASE small-caps with letter tracking
    tft.setFreeFont(&FreeSans9pt7b);
    String artistUp = s_artist;
    artistUp.toUpperCase();
    artistUp = fitText(artistUp, 200);
    drawTrackedCentered(artistUp, 120, 28, COLOR_TEXT_FAINT, 2);

    // Title — bold white, prominent
    tft.setFreeFont(&FreeSansBold12pt7b);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TEXT_HI);   // transparent bg
    String t = fitText(s_title, 210);
    tft.drawString(t, 120, 48);

    tft.setFreeFont(NULL);
    tft.setTextDatum(TL_DATUM);
}

void display_showTrackInfo(const String &title, const String &artist) {
    s_title  = title;
    s_artist = artist;
    renderTrackInfo();
}

// ─── Lyric helpers ───────────────────────────────────────────────────────────
static int findSplits(const String &text, int maxW, int splits[3]) {
    splits[0] = splits[1] = splits[2] = -1;
    if (tft.textWidth(text) <= maxW) return 0;
    int nSplits = 0, segStart = 0, wordStart = 0, len = (int)text.length();
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == ' ') {
            if (tft.textWidth(text.substring(segStart, i)) > maxW && wordStart > segStart) {
                splits[nSplits++] = wordStart - 1;
                segStart = wordStart;
                if (nSplits >= 3) break;
            }
            wordStart = i + 1;
        }
    }
    return nSplits;
}

// Per-word karaoke colouring on dimmed album (no pill backing).
// Two-pass for spacing correctness:
//   PASS 1 — full line in white (transparent bg on dimmed art)
//   PASS 2 — over-paint past + active words
static void renderWords(const String &seg, int y,
                        int highlightWord, int wOffset) {
    tft.setTextDatum(TL_DATUM);
    int segW = tft.textWidth(seg);
    int x = 120 - segW / 2;
    if (x < 4) x = 4;

    tft.setTextColor(COLOR_TEXT_HI);          // transparent bg
    tft.drawString(seg, x, y);

    int wIdx = wOffset, start = 0, len = (int)seg.length();
    int wx = x;
    for (int i = 0; i <= len; i++) {
        if (i == len || seg[i] == ' ') {
            if (i > start) {
                String w = seg.substring(start, i);
                bool needRecolor = (wIdx == highlightWord) || (wIdx < highlightWord);
                if (needRecolor) {
                    uint16_t col = (wIdx == highlightWord) ? COLOR_ACCENT
                                                          : COLOR_WORD_PAST;
                    tft.setTextColor(col);
                    tft.drawString(w, wx, y);
                }
                wx += tft.textWidth(w);
            }
            if (i < len) {
                wx += tft.textWidth(" ");
                wIdx++;
            }
            start = i + 1;
        }
    }
}

static void renderArcCached();

// ─── Lyric stage (no pills — text floats on dimmed art) ──────────────────────
void display_showLyrics(const String &currentLine, const String &nextLine,
                        int highlightWord, bool clearBg) {
    if (clearBg) {
        if (s_albumCacheValid && s_albumCache) {
            tft.pushImage(0, ALBUM_CACHE_Y, 240, ALBUM_CACHE_H, s_albumCache);
        } else if (s_albumLoaded) {
            TJpgDec.drawJpg(0, 0, s_albumBuf, (uint32_t)s_albumLen);
            renderTrackInfo();
            renderArcCached();
        } else {
            tft.fillRect(0, ALBUM_CACHE_Y, 240, ALBUM_CACHE_H, COLOR_BLACK);
        }
    }

    if (currentLine.length() == 0) {
        if (clearBg) {
            tft.setFreeFont(&FreeSans9pt7b);
            tft.setTextDatum(TC_DATUM);
            tft.setTextColor(COLOR_TEXT_FAINT);
            tft.drawString("· no lyrics ·", 120, 124);
            tft.setFreeFont(NULL);
            tft.setTextDatum(TL_DATUM);
        }
        return;
    }

    // ── Pick font + sizing tier ──────────────────────────────────────────────
    int splits[3]; int nSplits, fontH, lineH;
    const GFXfont* font;

    tft.setFreeFont(&FreeSansBold12pt7b);
    nSplits = findSplits(currentLine, 200, splits);
    if (nSplits == 0) { font = &FreeSansBold12pt7b; fontH = 18; lineH = 26; }
    else {
        tft.setFreeFont(&FreeSans12pt7b);
        nSplits = findSplits(currentLine, 204, splits);
        if (nSplits <= 1) { font = &FreeSans12pt7b; fontH = 17; lineH = 25; }
        else {
            tft.setFreeFont(&FreeSans9pt7b);
            nSplits = findSplits(currentLine, 204, splits);
            font = &FreeSans9pt7b; fontH = 13; lineH = 19;
        }
    }
    tft.setFreeFont(font);

    int nLines = nSplits + 1;
    int blockH = (nLines - 1) * lineH + fontH;

    int startY = 122 - blockH / 2;
    if (startY < 86)            startY = 86;
    if (startY + blockH > 158)  startY = 158 - blockH;

    String parts[4];
    int    wOff[4] = {0, 0, 0, 0};
    int    from = 0;
    for (int i = 0; i < nLines; i++) {
        int to = (i < nSplits) ? splits[i] : (int)currentLine.length();
        parts[i] = currentLine.substring(from, to);
        from = (i < nSplits) ? splits[i] + 1 : (int)currentLine.length();
    }
    for (int s = 1; s < nLines; s++) {
        int w = 1;
        for (char c : parts[s-1]) if (c == ' ') w++;
        wOff[s] = wOff[s-1] + w;
    }

    for (int i = 0; i < nLines; i++)
        renderWords(parts[i], startY + i * lineH, highlightWord, wOff[i]);

    // Next line — italic faint, transparent bg
    if (nextLine.length() > 0) {
        tft.setFreeFont(&FreeSansOblique9pt7b);
        tft.setTextDatum(TC_DATUM);
        String nl = fitText(nextLine, 200);
        tft.setTextColor(COLOR_TEXT_FAINT);
        tft.drawString(nl, 120, 178);
        tft.setFreeFont(NULL);
        tft.setTextDatum(TL_DATUM);
    }
}

// ─── Progress arc + accent dot at 12 o'clock ─────────────────────────────────
//   Thin 3 px arc on the very edge, with a bright accent dot fixed at the top
//   as the "now playing" cue (Car Thing styling).
static void renderArcAt(int angle) {
    uint32_t a = (uint32_t)angle;
    if (a < 358)
        tft.drawSmoothArc(120, 120, 119, 116, a, 360, 0x0841, 0x0000, false);
    if (a >= 2)
        tft.drawSmoothArc(120, 120, 119, 116, 0, a, COLOR_ACCENT, 0x0841, false);
    // Accent dot at 12 o'clock
    tft.fillCircle(120, 6, 3, COLOR_ACCENT);
    tft.fillCircle(120, 6, 4, COLOR_ACCENT);   // glow ring
    tft.fillCircle(120, 6, 2, COLOR_TEXT_HI);  // bright core
}

static void renderArcCached() {
    if (s_lastDuration <= 0) return;
    int angle = (int)(constrain((float)s_lastProgress / (float)s_lastDuration, 0.0f, 1.0f) * 360.0f);
    renderArcAt(angle);
    s_lastArcAngle = angle;
}

void display_drawProgressArc(long progressMs, long durationMs) {
    s_lastProgress = progressMs;
    s_lastDuration = durationMs;
    if (durationMs <= 0) return;
    int angle = (int)(constrain((float)progressMs / (float)durationMs, 0.0f, 1.0f) * 360.0f);
    if (angle == s_lastArcAngle) return;
    if (s_lastArcAngle >= 0 && abs(angle - s_lastArcAngle) < 2) return;
    s_lastArcAngle = angle;
    renderArcAt(angle);
}
