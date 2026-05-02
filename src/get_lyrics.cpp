#include "get_lyrics.h"
#include <Arduino.h>

LyricLine lyrics[MAX_LYRIC_LINES];
int       lyricCount          = 0;
long      wordStartMs[MAX_WORD_ENTRIES];
int       wordTimestampCount  = 0;
String    trackTitle          = "";
String    trackArtist         = "";

void lyrics_clear() {
    lyricCount        = 0;
    wordTimestampCount = 0;
    for (int i = 0; i < MAX_LYRIC_LINES; i++) {
        lyrics[i].timestampMs = 0;
        lyrics[i].text        = "";
        lyrics[i].wordOffset  = -1;
        lyrics[i].wordCount   = 0;
    }
}

void lyrics_parse_ble(const String &raw) {
    lyrics_clear();
    int pos = 0;
    while (pos < (int)raw.length() && lyricCount < MAX_LYRIC_LINES) {
        int newline = raw.indexOf('\n', pos);
        if (newline < 0) newline = raw.length();
        String line = raw.substring(pos, newline);
        line.trim();

        // TRACK: header injected by the Android app
        if (line.startsWith("TRACK:")) {
            int pipe = line.indexOf('|', 6);
            if (pipe > 0) {
                trackTitle  = line.substring(6, pipe);
                trackArtist = line.substring(pipe + 1);
                trackTitle.trim();
                trackArtist.trim();
            }
            pos = newline + 1;
            continue;
        }

        int pipe = line.indexOf('|');
        if (pipe > 0) {
            long   ms   = line.substring(0, pipe).toInt();
            String text = line.substring(pipe + 1);
            text.trim();
            if (text.length() > 0) {
                lyrics[lyricCount].timestampMs = ms;
                lyrics[lyricCount].text        = text;
                lyrics[lyricCount].wordOffset  = -1;
                lyrics[lyricCount].wordCount   = 0;
                lyricCount++;
            }
        }
        pos = newline + 1;
    }
    Serial.printf("[Lyrics] Parsed %d lines — %s / %s\n",
                  lyricCount, trackTitle.c_str(), trackArtist.c_str());
}

void lyrics_printAll() {
    for (int i = 0; i < lyricCount; i++)
        Serial.println("[" + String(lyrics[i].timestampMs) + "ms] " + lyrics[i].text);
}
