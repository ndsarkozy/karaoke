#pragma once
#include <Arduino.h>

#define MAX_LYRIC_LINES  200
#define MAX_WORD_ENTRIES 600

struct LyricLine {
    long   timestampMs;
    String text;
    int    wordOffset;
    int    wordCount;
};

extern LyricLine lyrics[MAX_LYRIC_LINES];
extern int       lyricCount;
extern long      wordStartMs[MAX_WORD_ENTRIES];
extern int       wordTimestampCount;
extern String    trackTitle;
extern String    trackArtist;

void lyrics_clear();
void lyrics_parse_ble(const String &raw);
void lyrics_printAll();
