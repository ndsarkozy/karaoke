#include "lyric_sync.h"
#include "get_lyrics.h"
#include <Arduino.h>

int sync_getCurrentLine(long progressMs) {
    if (lyricCount == 0) return -1;

    int current = 0;
    for (int i = 0; i < lyricCount; i++) {
        if (lyrics[i].timestampMs <= progressMs) {
            current = i;
        } else {
            break;
        }
    }
    return current;
}

int sync_getNextLine(long progressMs) {
    int current = sync_getCurrentLine(progressMs);
    if (current < 0) return -1;
    if (current + 1 < lyricCount) return current + 1;
    return current;
}

void sync_getDisplayLines(long progressMs, int* lineIndices, int maxLines) {
    if (lyricCount == 0) {
        lineIndices[0] = -1;
        return;
    }

    int current = sync_getCurrentLine(progressMs);

    for (int i = 0; i < maxLines; i++) {
        int lineIdx = current + i;
        if (lineIdx < lyricCount) {
            lineIndices[i] = lineIdx;
        } else {
            lineIndices[i] = -1;
        }
    }
}

int sync_getCurrentWord(long progressMs, int lineIdx) {
    if (lineIdx < 0 || lineIdx >= lyricCount) return -1;
    if (lyrics[lineIdx].wordOffset < 0)        return -1;

    int offset = lyrics[lineIdx].wordOffset;
    int count  = lyrics[lineIdx].wordCount;
    int current = 0;

    for (int i = 0; i < count; i++) {
        if (wordStartMs[offset + i] <= progressMs) {
            current = i;
        } else {
            break;
        }
    }
    return current;
}