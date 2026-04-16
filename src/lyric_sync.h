#pragma once
#include <Arduino.h>

#define MAX_DISPLAY_LINES 4  // Show current + 3 upcoming lines

int  sync_getCurrentLine(long progressMs);
int  sync_getNextLine(long progressMs);

// Get up to N upcoming lines (including current)
// Returns array of line indices, -1 marks end
void sync_getDisplayLines(long progressMs, int* lineIndices, int maxLines);