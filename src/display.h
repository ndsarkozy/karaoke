#pragma once
#include <Arduino.h>

// Standard colors
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_GRAY    0x8410

// ─── Refined design palette ────────────────────────────────────────────
// Tuned for the GC9A01 round panel under typical album-art backgrounds.
#define SPOTIFY_GREEN    0x07E8   // brighter accent #00FF40 — pops on dark wash
#define COLOR_ACCENT     0x07E8   // alias for clarity
#define COLOR_ACCENT_DIM 0x0440   // dim accent for separators / hairlines
#define COLOR_WORD_PAST  0x6B4D   // sung words (dim gray)
#define COLOR_NEXT_LINE  0x73AE   // next lyric (refined slate)
#define COLOR_TEXT_HI    0xFFFF   // primary text
#define COLOR_TEXT_MID   0xCE79   // secondary text
#define COLOR_TEXT_FAINT 0x52AA   // tertiary / muted preview
#define OVERLAY_DARK     0x10A2   // refined frosted-glass band (slightly lighter than near-black)
#define OVERLAY_DEEP     0x0841   // deeper overlay for the top info bar
#define HAIRLINE_DIM     0x2965   // subtle separator line

#define SCREEN_W        240
#define SCREEN_H        240
#define SCREEN_CENTER_X 120
#define SCREEN_CENTER_Y 120

void display_init();
void display_fill(uint16_t color);
void display_clear();
void display_print(const String &text, int x, int y, uint16_t color, uint8_t size);
void display_drawCircle(int x, int y, int r, uint16_t color);
void display_fillCircle(int x, int y, int r, uint16_t color);
void display_brightness(uint8_t value);
void display_showMessage(const String &line1, const String &line2, uint16_t color);

// New design API
bool display_drawAlbum(const uint8_t* buf, size_t len);
void display_showTrackInfo(const char* title, const char* artist);
void display_showLyrics(const String &currentLine, const String &nextLine, int highlightWord = 0, bool clearBg = true);
