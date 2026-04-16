#pragma once
#include <Arduino.h>

#define MAX_LYRIC_LINES 300

struct LyricLine {
    long   timestampMs;
    String text;
};

extern LyricLine lyrics[MAX_LYRIC_LINES];
extern int       lyricCount;

bool lyrics_fetch(const String &title, const String &artist);
void lyrics_clear();
void lyrics_printAll();