#pragma once
#include <Arduino.h>

void   ble_init();
bool   ble_isConnected();
bool   ble_newLyricsAvailable();
bool   ble_newProgressAvailable();
long   ble_getProgressMs();
bool   ble_getIsPlaying();
String ble_getLyrics();
void   ble_task(void* pv);
