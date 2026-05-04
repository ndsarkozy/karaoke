#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();

// ─── Cached state ────────────────────────────────────────────────────────────
static String s_title  = "";
static String s_artist = "";

// ─── Album cache (full-color) ────────────────────────────────────────────────
// Captures the decoded album rows when an album arrives. On every subsequent
// lyric repaint we pushImage these cached pixels back — the JPEG is never
// decoded a second time, so we never read the BLE buffer after the initial
// decode (kills the read-during-write race that was causing jumbled images).
//
// Two caches:
//   • s_albumCache (static BSS) covers the current-line strike zone y∈[86,158]
//     — must always exist, so it lives in BSS.
//   • s_albumCacheLower (heap, lazy) covers the next-line italic strip
//     y∈[158,200] — lazy heap alloc avoids the BSS overflow at link time
//     and the BT-startup heap starvation that bit us at boot.
// Together they cover y=86..200 with no black gap, so lyric text overlays
// the full-color album with no visible black rectangles.
#define ALBUM_CACHE_Y      86
#define ALBUM_CACHE_H      72
static uint16_t  s_albumCache[240 * ALBUM_CACHE_H];   // 240 × 72 × 2 = 34 560 B BSS
static bool      s_albumCacheValid = false;
static bool      s_capturingAlbum  = false;

// ─── JPEG callback: push full-color block, capture to both caches ───────────
// No dim transform — the album shows in full color and lyric legibility is
// handled by per-glyph outlining at draw time.
static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bm) {
    int16_t x1 = max(x, (int16_t)0), y1 = max(y, (int16_t)0);
    int16_t x2 = min((int16_t)(x + w), (int16_t)240);
    int16_t y2 = min((int16_t)(y + h), (int16_t)240);
    if (x1 >= x2 || y1 >= y2) return true;

    tft.pushImage(x1, y1, x2-x1, y2-y1, bm + (y1-y)*w + (x1-x));

    if (s_capturingAlbum) {
        // Upper strip (current-line zone) into static BSS cache.
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
void display_clear()            { tft.fillScreen(COLOR_BLACK); s_albumCacheValid = false; }
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
// Decode the JPEG to the screen. We query the JPEG's real dimensions first,
// pick the smallest TJpgDec scale that makes it fit inside 240×240, and
// center the decoded image. Without this, JPEGs that aren't exactly 240×240
// either overflow (large) or only fill the top-left (small).
//
// As blocks go by, snapshot the lyric-strike-zone rows into the static
// cache. After this returns the caller may release/overwrite `buf` — we
// never read it again.
bool display_drawAlbum(const uint8_t* buf, size_t len) {
    if (!buf || len == 0) return false;

    // Query source size so we can pick a scale.
    uint16_t srcW = 0, srcH = 0;
    if (TJpgDec.getJpgSize(&srcW, &srcH, buf, (uint32_t)len) != JDR_OK ||
        srcW == 0 || srcH == 0) {
        Serial.println("[Display] JPEG size probe failed");
        return false;
    }

    // Pick the smallest scale (1, 2, 4, 8) that makes the image fit.
    uint8_t scale = 1;
    while (scale < 8 && (srcW / scale > 240 || srcH / scale > 240)) scale <<= 1;
    TJpgDec.setJpgScale(scale);

    int dispW = srcW / scale;
    int dispH = srcH / scale;
    int offX  = (240 - dispW) / 2;
    int offY  = (240 - dispH) / 2;

    Serial.printf("[Display] Album: src=%ux%u scale=%u → %dx%d at (%d,%d)\n",
                  srcW, srcH, scale, dispW, dispH, offX, offY);

    // Wipe the screen first so undersized images don't leave old pixels in
    // the borders.
    tft.fillScreen(COLOR_BLACK);

    s_albumCacheValid = false;
    s_capturingAlbum  = true;
    JRESULT res = TJpgDec.drawJpg(offX, offY, buf, (uint32_t)len);
    s_capturingAlbum  = false;

    if (res != JDR_OK) {
        Serial.printf("[Display] JPEG decode failed (%d)\n", (int)res);
        return false;
    }
    s_albumCacheValid = true;
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

void display_showTrackInfo(const String &title, const String &artist) {
    s_title  = title;
    s_artist = artist;
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

// ─── Lyric stage (no pills — text floats on dimmed art) ──────────────────────
void display_showLyrics(const String &currentLine, const String &nextLine,
                        int highlightWord, bool clearBg) {
    if (clearBg) {
        // Single source of truth for the lyric backdrop: the cached dimmed
        // album rows. No JPEG re-decode path — that used to read the BLE
        // buffer concurrently with the BLE callback writing into it.
        if (s_albumCacheValid) {
            tft.pushImage(0, ALBUM_CACHE_Y, 240, ALBUM_CACHE_H, s_albumCache);
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
    (void)nextLine;
}

