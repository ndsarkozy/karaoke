#pragma once
#include <Arduino.h>

// Colors
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

// Screen dimensions
#define SCREEN_W 240
#define SCREEN_H 240
#define SCREEN_CENTER_X 120
#define SCREEN_CENTER_Y 120

void display_init();
void display_fill(uint16_t color);
void display_print(const String &text, int x, int y,
                   uint16_t color, uint8_t size);
void display_clear();
void display_drawCircle(int x, int y, int r, uint16_t color);
void display_fillCircle(int x, int y, int r, uint16_t color);
void display_brightness(uint8_t value);

// Higher level helpers
void display_showMessage(const String &line1, const String &line2, uint16_t color);
void display_showTrack(const String &title, const String &artist);
void display_showTrackText(const String &title, const String &artist);
void display_showLyric(const String &current, const String &next);
bool display_drawJpeg(const uint8_t* buf, size_t len, int x, int y);
// 2-row karaoke display: current line with per-word highlight, next line dimmed
void display_showLyrics(const String &currentLine, const String &nextLine, int highlightWord = 0);