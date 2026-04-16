#pragma once
#include <Arduino.h>

#define MAX_DISPLAY_LINES 4  // Show current + 3 upcoming lines

int  sync_getCurrentLine(long progressMs);
int  sync_getNextLine(long progressMs);
void sync_getDisplayLines(long progressMs, int* lineIndices, int maxLines);

// Returns the word index (0-based) within lineIdx that is currently being sung.
// Returns -1 if this line has no word timestamps (fall back to estimation).
int  sync_getCurrentWord(long progressMs, int lineIdx);