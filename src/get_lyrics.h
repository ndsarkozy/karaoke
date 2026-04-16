#pragma once
#include <Arduino.h>

#define MAX_LYRIC_LINES  200
#define MAX_WORD_ENTRIES 600

struct LyricLine {
    long   timestampMs;
    String text;
    int    wordOffset;  // index into wordStartMs[], -1 = no word timestamps
    int    wordCount;
};

extern LyricLine lyrics[MAX_LYRIC_LINES];
extern int       lyricCount;

extern long wordStartMs[MAX_WORD_ENTRIES];
extern int  wordTimestampCount;

bool lyrics_fetch(const String &title, const String &artist);
void lyrics_clear();
void lyrics_printAll();