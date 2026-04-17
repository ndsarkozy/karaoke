#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Arduino.h>

#define LYRIC_BG 0x0841

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

// Returns the char index of the best space to split on, -1 if fits on one line
static int findSplitChar(const String &text, int font, int maxW) {
    tft.setTextFont(font);
    if (tft.textWidth(text) <= maxW) return -1;

    int best   = -1;
    int bestMax = 99999;

    for (int i = 1; i < (int)text.length(); i++) {
        if (text[i] != ' ') continue;
        String left  = text.substring(0, i);
        String right = text.substring(i + 1);
        int lw = tft.textWidth(left);
        int rw = tft.textWidth(right);
        if (lw <= maxW && rw <= maxW) {
            int mx = max(lw, rw);
            if (mx < bestMax) { bestMax = mx; best = i; }
        }
    }
    return best;
}

// Draw a segment of text word-by-word with karaoke coloring:
//   past words = gray, current = yellow, future = white
// wOffset = how many words precede this segment in the full line
static void renderWords(const String &seg, int y, int font,
                        int highlightWord, int wOffset) {
    tft.setTextFont(font);
    tft.setTextDatum(TL_DATUM);

    int totalW = tft.textWidth(seg);
    int x = SCREEN_CENTER_X - totalW / 2;
    if (x < 3) x = 3;

    int wIdx  = wOffset;
    int start = 0;
    int len   = (int)seg.length();

    for (int i = 0; i <= len; i++) {
        if (i == len || seg[i] == ' ') {
            if (i > start) {
                String w = seg.substring(start, i);
                uint16_t col;
                if      (wIdx < highlightWord)  col = 0x7BEF;
                else if (wIdx == highlightWord) col = COLOR_YELLOW;
                else                            col = COLOR_WHITE;
                tft.setTextColor(col, LYRIC_BG);
                tft.drawString(w, x, y);
                x += tft.textWidth(w);
            }
            if (i < len) {
                tft.setTextColor(COLOR_WHITE, LYRIC_BG);
                tft.drawString(" ", x, y);
                x += tft.textWidth(" ");
                wIdx++;
            }
            start = i + 1;
        }
    }
}

void display_showLyrics(const String &currentLine, const String &nextLine, int highlightWord) {
    static String lastLine = "";
    bool lineChanged = (currentLine != lastLine);

    if (lineChanged) {
        // Fill lyric zone with dark panel (art stays visible above y=88)
        tft.fillRect(0, 88, 240, 92, LYRIC_BG);
        tft.drawFastHLine(50, 138, 140, 0x2104);
        lastLine = currentLine;
    }

    if (currentLine.length() == 0) return;

    // Choose font + split strategy:
    //   font2 1-line → font2 2-line → font1 1-line → font1 2-line
    int font    = 2;
    int splitAt = -1;

    tft.setTextFont(2);
    if (tft.textWidth(currentLine) > 200) {
        splitAt = findSplitChar(currentLine, 2, 200);
        if (splitAt < 0) {
            font    = 1;
            splitAt = findSplitChar(currentLine, 1, 225);
        }
    }

    if (splitAt < 0) {
        // Single line — vertically centered near display center
        renderWords(currentLine, 112, font, highlightWord, 0);
    } else {
        // Two lines — count word offset for second segment
        String part1 = currentLine.substring(0, splitAt);
        String part2 = currentLine.substring(splitAt + 1);

        int wOffset = 0;
        for (char c : part1) if (c == ' ') wOffset++;
        wOffset++;  // part2 begins at the next word

        int y1 = (font == 2) ? 97 : 102;
        int y2 = (font == 2) ? 117 : 114;
        renderWords(part1, y1, font, highlightWord, 0);
        renderWords(part2, y2, font, highlightWord, wOffset);
    }

    // Next line: very dim, font 1, drawn in-place (no clear needed)
    tft.setTextFont(1);
    tft.setTextDatum(MC_DATUM);
    if (nextLine.length() > 0) {
        tft.setTextColor(0x4208, LYRIC_BG);
        tft.drawString(nextLine.substring(0, 38), SCREEN_CENTER_X, 152);
    } else {
        tft.setTextColor(LYRIC_BG, LYRIC_BG);
        tft.drawString("                                      ", SCREEN_CENTER_X, 152);
    }
}