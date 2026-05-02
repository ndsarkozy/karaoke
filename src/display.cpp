#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();

// Cached album buffer so lyrics can redraw art to clear old text
static const uint8_t* s_albumBuf     = nullptr;
static size_t         s_albumLen     = 0;
static bool           s_albumLoaded  = false;
static int            s_lastArcAngle = -1;

// Cached track info so display_showLyrics can re-stamp it after art redraw
static String s_title  = "";
static String s_artist = "";

// ── JPEG callback (center-crop to 240×240) ───────────────────────────────────
static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bm) {
    int16_t x1 = max(x, (int16_t)0), y1 = max(y, (int16_t)0);
    int16_t x2 = min((int16_t)(x + w), (int16_t)240);
    int16_t y2 = min((int16_t)(y + h), (int16_t)240);
    if (x1 >= x2 || y1 >= y2) return true;
    tft.pushImage(x1, y1, x2-x1, y2-y1, bm + (y1-y)*w + (x1-x));
    return true;
}

// Dark wash sized for the GC9A01 circle — widest usable band starts ~y=40
static void applyTopOverlay() {
    tft.fillRect(0, 38, 240, 32, 0x1082);   // y=38-70, ~210 px wide at center
    tft.drawFastHLine(20, 70, 200, SPOTIFY_GREEN);
}

// ── Public API ────────────────────────────────────────────────────────────────
void display_init() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(jpegOutput);
    Serial.println("[Display] Initialized");
}

void display_fill(uint16_t c)   { tft.fillScreen(c); }
void display_clear()            { tft.fillScreen(COLOR_BLACK); s_albumLoaded = false; s_lastArcAngle = -1; }
void display_drawCircle(int x, int y, int r, uint16_t c) { tft.drawCircle(x,y,r,c); }
void display_fillCircle(int x, int y, int r, uint16_t c) { tft.fillCircle(x,y,r,c); }
void display_brightness(uint8_t v) { analogWrite(TFT_BL, v); }

void display_print(const String &text, int x, int y, uint16_t color, uint8_t size) {
    tft.setTextColor(color, COLOR_BLACK);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

void display_showMessage(const String &l1, const String &l2, uint16_t color) {
    display_clear();
    display_print(l1, 20, 100, color, 2);
    display_print(l2, 20, 130, color, 2);
}

// ── Album art ─────────────────────────────────────────────────────────────────
bool display_drawAlbum(const uint8_t* buf, size_t len) {
    if (!buf || len == 0) return false;
    JRESULT res = TJpgDec.drawJpg(0, 0, buf, (uint32_t)len);
    if (res != JDR_OK) return false;
    s_albumBuf    = buf;
    s_albumLen    = len;
    s_albumLoaded = true;
    s_lastArcAngle = -1;
    applyTopOverlay();
    return true;
}

// ── Track info ────────────────────────────────────────────────────────────────
void display_showTrackInfo(const String &title, const String &artist) {
    s_title  = title;
    s_artist = artist;

    applyTopOverlay();
    tft.setTextDatum(TC_DATUM);

    // At y=42 the GC9A01 circle is ~210 px wide — safe for both lines
    // Artist — Spotify green, font 1 (8 px)
    tft.setTextFont(1);
    String a = artist;
    while (a.length() > 1 && tft.textWidth(a) > 180) a = a.substring(0, a.length()-1);
    if (a.length() < artist.length()) a += "..";
    tft.setTextColor(COLOR_BLACK);
    tft.drawString(a, 121, 43);
    tft.setTextColor(SPOTIFY_GREEN);
    tft.drawString(a, 120, 42);

    // Title — white, font 1 (keep narrow so it fits the circle at y~54)
    tft.setTextFont(1);
    String t = title;
    while (t.length() > 1 && tft.textWidth(t) > 180) t = t.substring(0, t.length()-1);
    if (t.length() < title.length()) t += "..";
    tft.setTextColor(COLOR_BLACK);
    tft.drawString(t, 121, 55);
    tft.setTextColor(COLOR_WHITE);
    tft.drawString(t, 120, 54);
}

// ── Lyric helpers ─────────────────────────────────────────────────────────────

// Greedy word-wrap; returns number of split points (0 = fits on 1 line).
static int findSplits(const String &text, int font, int maxW, int splits[3]) {
    splits[0] = splits[1] = splits[2] = -1;
    tft.setTextFont(font);
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

// Draw one line segment with per-word karaoke colouring.
// Uses setCursor+print (most basic TFT_eSPI path) to avoid any datum issues.
static void renderWords(const String &seg, int y, int font,
                        int highlightWord, int wOffset) {
    tft.setTextFont(font);
    tft.setTextSize(1);
    int segW = tft.textWidth(seg);
    int x = 120 - segW / 2;
    if (x < 4) x = 4;

    int wIdx = wOffset, start = 0, len = (int)seg.length();
    for (int i = 0; i <= len; i++) {
        if (i == len || seg[i] == ' ') {
            if (i > start) {
                String w = seg.substring(start, i);
                uint16_t col = (wIdx == highlightWord) ? SPOTIFY_GREEN
                             : (wIdx  < highlightWord) ? COLOR_WORD_PAST
                                                       : COLOR_WHITE;
                tft.setTextColor(col, OVERLAY_DARK);
                tft.setCursor(x, y);
                tft.print(w);
                x += tft.textWidth(w);
            }
            if (i < len) {
                x += tft.textWidth(" ");
                wIdx++;
            }
            start = i + 1;
        }
    }
}

// ── Lyric display ─────────────────────────────────────────────────────────────
//
// clearBg=true  → redraw album art + track info to wipe old line before drawing
// clearBg=false → word-colour update on the same line; just overdraw text pixels
//
void display_showLyrics(const String &currentLine, const String &nextLine,
                        int highlightWord, bool clearBg) {
    if (clearBg) {
        if (s_albumLoaded)
            TJpgDec.drawJpg(0, 0, s_albumBuf, (uint32_t)s_albumLen);
        else
            tft.fillScreen(COLOR_BLACK);
        display_showTrackInfo(s_title, s_artist);
        s_lastArcAngle = -1;
    }

    if (currentLine.length() == 0) {
        if (clearBg) {
            tft.fillRect(0, 104, 240, 24, OVERLAY_DARK);
            tft.setTextFont(1);
            tft.setTextSize(1);
            int sw = tft.textWidth("No lyrics available");
            tft.setTextColor(COLOR_GRAY, OVERLAY_DARK);
            tft.setCursor(120 - sw/2, 108);
            tft.print("No lyrics available");
        }
        return;
    }

    // Font selection: font 4 (26 px) if single line fits, else font 2 (16 px)
    int font, fontH, lineH;
    {
        int sp[3];
        // Use 190 px max width — safe across the circle between y=80 and y=165
        int ns = findSplits(currentLine, 4, 190, sp);
        if (ns == 0) { font = 4; fontH = 26; lineH = 34; }
        else         { font = 2; fontH = 16; lineH = 22; }
    }

    int splits[3];
    int nSplits = findSplits(currentLine, font, 190, splits);
    int nLines  = nSplits + 1;
    int blockH  = (nLines - 1) * lineH + fontH;

    // True screen centre is y=120.  Lyrics centred there, bounded so they
    // don't overlap the info bar (y<78) or the next-line area (y>168).
    int startY = 120 - blockH / 2;
    if (startY < 78)          startY = 78;
    if (startY + blockH > 168) startY = 168 - blockH;

    Serial.printf("[Lyric] font=%d nLines=%d blockH=%d startY=%d text='%s'\n",
                  font, nLines, blockH, startY, currentLine.c_str());

    // Dark band behind lyrics so text is always readable over any album art
    tft.fillRect(0, startY - 4, 240, blockH + 8, OVERLAY_DARK);

    // Build line segments and cumulative word offsets
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
        renderWords(parts[i], startY + i * lineH, font, highlightWord, wOff[i]);

    // Next line — font 1, muted, at y=178 (circle is ~200 px wide there — safe)
    if (nextLine.length() > 0) {
        tft.setTextFont(1);
        tft.setTextDatum(TC_DATUM);
        String nl = nextLine;
        while (nl.length() > 1 && tft.textWidth(nl) > 170) {
            int sp = nl.lastIndexOf(' ');
            nl = (sp > 0) ? nl.substring(0, sp) : nl.substring(0, nl.length()-1);
        }
        tft.setTextColor(COLOR_BLACK);
        tft.drawString(nl, 121, 179);
        tft.setTextColor(COLOR_NEXT_LINE);
        tft.drawString(nl, 120, 178);
    }
}

// ── Progress arc ──────────────────────────────────────────────────────────────
void display_drawProgressArc(long progressMs, long durationMs) {
    if (durationMs <= 0) return;
    int angle = (int)(constrain((float)progressMs / (float)durationMs, 0.0f, 1.0f) * 360.0f);
    if (angle == s_lastArcAngle) return;
    if (s_lastArcAngle >= 0 && abs(angle - s_lastArcAngle) < 2) return;
    s_lastArcAngle = angle;

    uint32_t a = (uint32_t)angle;
    if (a < 358)
        tft.drawSmoothArc(120, 120, 117, 113, a, 360, 0x2104, 0x0000, false);
    if (a >= 2)
        tft.drawSmoothArc(120, 120, 117, 113, 0, a, SPOTIFY_GREEN, 0x2104, false);
}
