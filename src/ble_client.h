#pragma once
#include <Arduino.h>

void   ble_init();
bool   ble_isConnected();
bool   ble_newLyricsAvailable();
bool   ble_newProgressAvailable();
bool   ble_newAlbumAvailable();
long   ble_getProgressMs();
long   ble_getDurationMs();
bool   ble_getIsPlaying();
String ble_getLyrics();
const uint8_t* ble_getAlbumBuf();
size_t         ble_getAlbumLen();
void   ble_task(void* pv);
