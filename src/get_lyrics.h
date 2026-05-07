#pragma once
#include <Arduino.h>

#define MAX_LYRIC_LINES  150
#define MAX_WORD_ENTRIES 450
#define MAX_LINE_LEN     80

struct LyricLine {
    long timestampMs;
    char text[MAX_LINE_LEN + 1];
    int  wordOffset;
    int  wordCount;
};

extern LyricLine lyrics[MAX_LYRIC_LINES];
extern int       lyricCount;
extern long      wordStartMs[MAX_WORD_ENTRIES];
extern int       wordTimestampCount;
extern char      trackTitle[64];
extern char      trackArtist[64];

void lyrics_clear();
void lyrics_parse_ble(const char* raw);
void lyrics_printAll();
