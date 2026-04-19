#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>

// Wide dark band covers center; art visible at top (0-60) and bottom (197-240)
#define BAND_FADE_TOP   60   // gradient fade-in starts
#define BAND_SOLID_TOP  75   // solid dark zone begins
#define BAND_SOLID_BTM  182  // solid dark zone ends
#define BAND_FADE_BTM   197  // gradient fade-out ends
#define LYRIC_CLEAR_TOP 75   // fillRect before each lyric redraw
#define NEXT_LINE_Y     168  // top-y of dimmed next-line (within solid zone)

static TFT_eSPI tft = TFT_eSPI();

static bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    // Clip to display bounds (handles negative offsets for center-crop)
    int16_t x1 = max(x, (int16_t)0);
    int16_t y1 = max(y, (int16_t)0);
    int16_t x2 = min((int16_t)(x + w), (int16_t)240);
    int16_t y2 = min((int16_t)(y + h), (int16_t)240);
    if (x1 >= x2 || y1 >= y2) return true;
    uint16_t* bm = bitmap + (y1 - y) * w + (x1 - x);
    tft.pushImage(x1, y1, x2 - x1, y2 - y1, bm);
    return true;
}

void display_init() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);

    TJpgDec.setJpgScale(1);        // full resolution — we center-crop in the callback
    TJpgDec.setSwapBytes(true);    // required for ESP32
    TJpgDec.setCallback(jpegOutput);

    Serial.println("[Display] Initialized");
}

void display_fill(uint16_t color) {
    tft.fillScreen(color);
}

void display_clear() {
    tft.fillScreen(COLOR_BLACK);
}

void display_print(const String &text, int x, int y,
                   uint16_t color, uint8_t size) {
    tft.setTextColor(color, COLOR_BLACK);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

void display_drawCircle(int x, int y, int r, uint16_t color) {
    tft.drawCircle(x, y, r, color);
}

void display_fillCircle(int x, int y, int r, uint16_t color) {
    tft.fillCircle(x, y, r, color);
}

void display_brightness(uint8_t value) {
    // If backlight is on a PWM pin, use analogWrite
    // Otherwise just on/off
    analogWrite(TFT_BL, value);
}

void display_showMessage(const String &line1,
                         const String &line2,
                         uint16_t color) {
    display_clear();
    display_print(line1, 20, 100, color, 2);
    display_print(line2, 20, 130, color, 2);
}

void display_showTrack(const String &title, const String &artist) {
    display_clear();
    // Dark placeholder fills the circle while art loads
    tft.fillCircle(SCREEN_CENTER_X, SCREEN_CENTER_Y, 119, 0x1082);
    display_showTrackText(title, artist);
}

void display_showTrackText(const String &title, const String &artist) {
    // Step-gradient dark bar over lower portion of the circle
    tft.fillRect(0, 148, 240, 18, 0x2104);
    tft.fillRect(0, 166, 240, 14, 0x1082);
    tft.fillRect(0, 180, 240, 60, 0x0000);

    tft.setTextDatum(MC_DATUM);

    // Title — white, font 2
    tft.setTextFont(2);
    String t = title;
    while (t.length() > 1 && tft.textWidth(t) > 210) t.remove(t.length() - 1);
    tft.setTextColor(COLOR_WHITE, 0x0000);
    tft.drawString(t, SCREEN_CENTER_X, 185);

    // Artist — green, font 1
    tft.setTextFont(1);
    tft.setTextColor(COLOR_GREEN, 0x0000);
    tft.drawString(artist.substring(0, 32), SCREEN_CENTER_X, 204);
}

bool display_drawJpeg(const uint8_t* buf, size_t len, int x, int y) {
    if (!buf || len == 0) return false;
    JRESULT res = TJpgDec.drawJpg((int16_t)x, (int16_t)y, buf, (uint32_t)len);
    return (res == JDR_OK);
}

void display_applyLyricGradient() {
    // Top fade: art → black (y=60–75)
    tft.fillRect(0,  60, 240, 4, 0x2104);
    tft.fillRect(0,  64, 240, 4, 0x1082);
    tft.fillRect(0,  68, 240, 4, 0x0841);
    tft.fillRect(0,  72, 240, 3, 0x0421);
    // Solid dark band where lyrics live (y=75–182)
    tft.fillRect(0,  75, 240, 107, COLOR_BLACK);
    // Bottom fade: black → art (y=182–197)
    tft.fillRect(0, 182, 240, 4, 0x0421);
    tft.fillRect(0, 186, 240, 4, 0x0841);
    tft.fillRect(0, 190, 240, 4, 0x1082);
    tft.fillRect(0, 194, 240, 3, 0x2104);
}

void display_showLyric(const String &current, const String &next) {
    tft.fillRect(0, 130, 240, 110, COLOR_BLACK);

    tft.setTextDatum(MC_DATUM);

    // Current lyric in white, size 2
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(2);
    if (current.length() <= 12) {
        tft.drawString(current, SCREEN_CENTER_X, 160);
    } else {
        // Split into two lines at nearest space
        int split = current.lastIndexOf(' ', 12);
        if (split < 0) split = 12;
        tft.drawString(current.substring(0, split), SCREEN_CENTER_X, 148);
        tft.drawString(current.substring(split + 1), SCREEN_CENTER_X, 170);
    }

    // Next lyric in gray, size 1
    tft.setTextColor(COLOR_GRAY, COLOR_BLACK);
    tft.setTextSize(1);
    tft.drawString(next.substring(0, 30), SCREEN_CENTER_X, 200);
}

// Greedy word-wrap: fill each line left-to-right, split when next word overflows.
// Supports up to 3 splits (4 lines). splits[] receives space indices where line breaks go.
static int findSplits(const String &text, int font, int maxW, int splits[3]) {
    splits[0] = splits[1] = splits[2] = -1;
    tft.setTextFont(font);
    if (tft.textWidth(text) <= maxW) return 0;

    int nSplits  = 0;
    int segStart = 0;
    int wordStart = 0;
    int len = (int)text.length();

    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == ' ') {
            // Check if text from segStart up to (not including) this space fits
            String candidate = text.substring(segStart, i);
            if (tft.textWidth(candidate) > maxW && wordStart > segStart) {
                // Split before the current word (space is at wordStart-1)
                splits[nSplits++] = wordStart - 1;
                segStart = wordStart;
                if (nSplits >= 3) break;
            }
            wordStart = i + 1;
        }
    }
    return nSplits;
}

// Draw one segment word-by-word with karaoke colouring (transparent background).
// wOffset = number of words that precede this segment in the full line.
static void renderWords(const String &seg, int y, int font,
                        int highlightWord, int wOffset) {
    tft.setTextFont(font);
    tft.setTextDatum(TL_DATUM);

    int totalW = tft.textWidth(seg);
    int x = SCREEN_CENTER_X - totalW / 2;
    if (x < 2) x = 2;

    int wIdx = wOffset, start = 0, len = (int)seg.length();

    for (int i = 0; i <= len; i++) {
        if (i == len || seg[i] == ' ') {
            if (i > start) {
                String w = seg.substring(start, i);
                uint16_t col;
                if      (wIdx < highlightWord)  col = 0x9492;       // past: medium gray
                else if (wIdx == highlightWord) col = COLOR_YELLOW;  // current: yellow
                else                            col = COLOR_WHITE;   // future: white
                tft.setTextColor(col);   // single-arg = transparent background
                tft.drawString(w, x, y);
                x += tft.textWidth(w);
            }
            if (i < len) {
                tft.setTextColor(COLOR_WHITE);
                tft.drawString(" ", x, y);
                x += tft.textWidth(" ");
                wIdx++;
            }
            start = i + 1;
        }
    }
}

void display_showLyrics(const String &currentLine, const String &nextLine, int highlightWord) {
    tft.fillRect(0, LYRIC_CLEAR_TOP, 240, BAND_SOLID_BTM - LYRIC_CLEAR_TOP, COLOR_BLACK);

    if (currentLine.length() == 0) return;

    // Font 2 (16px), up to 4 lines, 22px pitch (6px gap between lines)
    static const int FONT  = 2;
    static const int FONTH = 16;
    static const int LINEH = 22;

    int splits[3];
    int nSplits = findSplits(currentLine, FONT, 224, splits);
    int nLines  = nSplits + 1;
    int blockH  = (nLines - 1) * LINEH + FONTH;

    // Vertically centre block between LYRIC_CLEAR_TOP and NEXT_LINE_Y
    int zoneH  = NEXT_LINE_Y - LYRIC_CLEAR_TOP;
    int startY = LYRIC_CLEAR_TOP + (zoneH - blockH) / 2;
    if (startY < LYRIC_CLEAR_TOP) startY = LYRIC_CLEAR_TOP;

    // Build segments from split points
    String parts[4];
    int    wOffsets[4] = {0, 0, 0, 0};
    int    boundaries[4] = { 0,
        (nSplits >= 1) ? splits[0] + 1 : (int)currentLine.length(),
        (nSplits >= 2) ? splits[1] + 1 : (int)currentLine.length(),
        (nSplits >= 3) ? splits[2] + 1 : (int)currentLine.length()
    };
    for (int i = 0; i < nLines; i++) {
        int from = boundaries[i];
        int to   = (i < nSplits) ? splits[i] : (int)currentLine.length();
        parts[i] = currentLine.substring(from, to);
    }
    // Word offsets: cumulative word count up to each segment
    for (int s = 1; s < nLines; s++) {
        int w = 1;
        for (char c : parts[s-1]) if (c == ' ') w++;
        wOffsets[s] = wOffsets[s-1] + w;
    }

    for (int i = 0; i < nLines; i++)
        renderWords(parts[i], startY + i * LINEH, FONT, highlightWord, wOffsets[i]);

    // Next line: font 1 (8px), dimmed, centered
    if (nextLine.length() > 0) {
        tft.setTextFont(1);
        tft.setTextDatum(TC_DATUM);
        String nl = nextLine;
        while (nl.length() > 1 && tft.textWidth(nl) > 224) {
            int sp = nl.lastIndexOf(' ');
            nl = (sp > 0) ? nl.substring(0, sp) : nl.substring(0, nl.length() - 1);
        }
        tft.setTextColor(0x528A);
        tft.drawString(nl, SCREEN_CENTER_X, NEXT_LINE_Y);
    }
}