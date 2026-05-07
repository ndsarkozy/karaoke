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
const char* ble_getLyrics();   // valid until next ble_newLyricsAvailable() call
const uint8_t* ble_getAlbumBuf();
size_t         ble_getAlbumLen();
void           ble_lockAlbum();    // hold while reading albumBuf — blocks notify writes
void           ble_unlockAlbum();
void   ble_task(void* pv);
