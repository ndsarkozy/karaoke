#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <Arduino.h>

static TFT_eSPI tft = TFT_eSPI();

void display_init() {
    // Backlight on
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);
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

void display_showTrack(const String &title,
                       const String &artist) {
    display_clear();

    // Draw outer ring
    display_drawCircle(SCREEN_CENTER_X, SCREEN_CENTER_Y, 118, COLOR_GREEN);
    display_drawCircle(SCREEN_CENTER_X, SCREEN_CENTER_Y, 116, COLOR_GREEN);

    // Title
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(title.substring(0, 15), SCREEN_CENTER_X, 100);

    // Artist
    tft.setTextColor(COLOR_GREEN, COLOR_BLACK);
    tft.setTextSize(1);
    tft.drawString(artist.substring(0, 20), SCREEN_CENTER_X, 130);
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

void display_showLyrics(const String &currentLine, const String &nextLine, int highlightWord) {
    tft.fillRect(0, 85, 240, 95, COLOR_BLACK);

    if (currentLine.length() == 0) return;

    // Pick font 2 if the full line fits within 210px, else font 1
    tft.setTextFont(2);
    int font = (tft.textWidth(currentLine) <= 210) ? 2 : 1;
    tft.setTextFont(font);
    tft.setTextDatum(TL_DATUM);

    // Center the whole line horizontally
    int totalW = tft.textWidth(currentLine);
    int x = SCREEN_CENTER_X - totalW / 2;
    int y = 118;

    // Draw word by word, highlighting the active word in yellow
    int wordIdx = 0;
    int start   = 0;
    int len     = (int)currentLine.length();

    for (int i = 0; i <= len; i++) {
        if (i == len || currentLine[i] == ' ') {
            if (i > start) {
                String word = currentLine.substring(start, i);
                tft.setTextColor(wordIdx == highlightWord ? COLOR_YELLOW : COLOR_WHITE,
                                 COLOR_BLACK);
                tft.drawString(word, x, y);
                x += tft.textWidth(word);
            }
            if (i < len) {
                tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
                tft.drawString(" ", x, y);
                x += tft.textWidth(" ");
                wordIdx++;
            }
            start = i + 1;
        }
    }

    // Next line: smaller, gray, centered
    if (nextLine.length() > 0) {
        tft.setTextFont(1);
        tft.setTextColor(COLOR_GRAY, COLOR_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(nextLine, SCREEN_CENTER_X, 155);
    }
}