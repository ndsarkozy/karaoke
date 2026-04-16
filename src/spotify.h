#pragma once
#include <Arduino.h>

struct SpotifyTrack {
    String title;
    String artist;
    String trackId;
    String albumArtUrl;  // 300x300 JPEG
    long   progressMs;
    long   durationMs;
    bool   isPlaying;
};

bool spotify_refreshToken();
bool spotify_getNowPlaying(SpotifyTrack &track);
bool spotify_getLyrics(const String &trackId);