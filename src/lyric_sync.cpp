#include "lyric_sync.h"
#include "get_lyrics.h"

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