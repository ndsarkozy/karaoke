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

void display_showLyric(const String &current,
                       const String &next) {
    // Clear lyric area only
    tft.fillRect(10, 140, 220, 80, COLOR_BLACK);

    // Current lyric in white
    tft.setTextColor(COLOR_WHITE, COLOR_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(current.substring(0, 15), SCREEN_CENTER_X, 160);

    // Next lyric in gray
    tft.setTextColor(COLOR_GRAY, COLOR_BLACK);
    tft.setTextSize(1);
    tft.drawString(next.substring(0, 20), SCREEN_CENTER_X, 190);
}