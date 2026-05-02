#include "get_lyrics.h"
#include <Arduino.h>

LyricLine lyrics[MAX_LYRIC_LINES];
int       lyricCount          = 0;
long      wordStartMs[MAX_WORD_ENTRIES];
int       wordTimestampCount  = 0;
String    trackTitle          = "";
String    trackArtist         = "";

void lyrics_clear() {
    lyricCount         = 0;
    wordTimestampCount = 0;
    for (int i = 0; i < MAX_LYRIC_LINES; i++) {
        lyrics[i].timestampMs = 0;
        lyrics[i].text        = "";
        lyrics[i].wordOffset  = -1;
        lyrics[i].wordCount   = 0;
    }
}

void lyrics_parse_ble(const char* raw) {
    lyrics_clear();
    trackTitle  = "";
    trackArtist = "";
    if (!raw || !*raw) {
        Serial.println("[Lyrics] Parsed 0 lines — empty payload");
        return;
    }

    const char* p = raw;
    while (*p && lyricCount < MAX_LYRIC_LINES) {
        const char* nl = strchr(p, '\n');
        int lineLen = nl ? (int)(nl - p) : (int)strlen(p);

        // strip trailing \r
        int end = lineLen;
        while (end > 0 && (p[end-1] == '\r' || p[end-1] == ' ')) end--;

        if (end >= 6 && strncmp(p, "TRACK:", 6) == 0) {
            const char* pipe = (const char*)memchr(p + 6, '|', end - 6);
            if (pipe) {
                trackTitle  = String(p + 6, (int)(pipe - p - 6));
                trackArtist = String(pipe + 1, (int)(p + end - pipe - 1));
                trackTitle.trim();
                trackArtist.trim();
            }
        } else {
            const char* pipe = (const char*)memchr(p, '|', end);
            if (pipe && pipe > p) {
                char msBuf[12] = {};
                int msLen = min((int)(pipe - p), 11);
                memcpy(msBuf, p, msLen);
                long ms = atol(msBuf);

                const char* textStart = pipe + 1;
                int textLen = (int)(p + end - textStart);
                while (textLen > 0 && textStart[textLen-1] == ' ') textLen--;

                if (textLen > 0) {
                    lyrics[lyricCount].timestampMs = ms;
                    lyrics[lyricCount].text        = String(textStart, textLen);
                    lyrics[lyricCount].wordOffset  = -1;
                    lyrics[lyricCount].wordCount   = 0;
                    lyricCount++;
                }
            }
        }
        p = nl ? nl + 1 : p + lineLen;
    }
    Serial.printf("[Lyrics] Parsed %d lines — %s / %s\n",
                  lyricCount, trackTitle.c_str(), trackArtist.c_str());
}

void lyrics_printAll() {
    for (int i = 0; i < lyricCount; i++)
        Serial.println("[" + String(lyrics[i].timestampMs) + "ms] " + lyrics[i].text);
}
