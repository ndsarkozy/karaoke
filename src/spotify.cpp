#include "spotify.h"
#include "get_lyrics.h"
#include "config.h"
#include "net.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

static String accessToken = "";

bool spotify_refreshToken() {
    if (!wifi_connected()) {
        Serial.println("[Spotify] WiFi not connected");
        return false;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    http.begin(client, "https://accounts.spotify.com/api/token");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String creds = String(CLIENT_ID) + ":" + String(CLIENT_SECRET);
    String encoded = base64::encode(creds);
    http.addHeader("Authorization", "Basic " + encoded);

    String body = "grant_type=refresh_token&refresh_token=" + String(REFRESH_TOKEN);
    int code = http.POST(body);

    if (code != 200) {
        Serial.println("[Spotify] Token refresh failed, HTTP " + String(code));
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[Spotify] Token JSON parse error");
        return false;
    }

    accessToken = doc["access_token"].as<String>();
    Serial.println("[Spotify] Token refreshed OK");
    return true;
}

bool spotify_getNowPlaying(SpotifyTrack &track) {
    if (accessToken == "") {
        Serial.println("[Spotify] No access token");
        return false;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    http.begin(client, "https://api.spotify.com/v1/me/player/currently-playing");
    http.addHeader("Authorization", "Bearer " + accessToken);

    int code = http.GET();

    if (code == 204) {
        Serial.println("[Spotify] Nothing playing");
        http.end();
        return false;
    }

    if (code != 200) {
        Serial.println("[Spotify] Now playing failed, HTTP " + String(code));
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[Spotify] Now playing JSON parse error");
        return false;
    }

    track.title      = doc["item"]["name"].as<String>();
    track.artist     = doc["item"]["artists"][0]["name"].as<String>();
    track.trackId    = doc["item"]["id"].as<String>();
    track.progressMs = doc["progress_ms"].as<long>();
    track.durationMs = doc["item"]["duration_ms"].as<long>();
    track.isPlaying  = doc["is_playing"].as<bool>();

    Serial.println("[Spotify] Track: " + track.title);
    Serial.println("[Spotify] Artist: " + track.artist);
    Serial.println("[Spotify] Progress: " + String(track.progressMs / 1000) + "s");

    return true;
}

// ── Internal helpers ──────────────────────────────────────────────────────────

static bool parseLineSynced(JsonArray lines) {
    for (JsonObject line : lines) {
        if (lyricCount >= MAX_LYRIC_LINES) break;
        const char* ms  = line["startTimeMs"] | "0";
        const char* txt = line["words"]       | "";
        String text(txt);
        text.trim();
        if (text.isEmpty() || text == "\u266a") continue;

        lyrics[lyricCount].timestampMs = atol(ms);
        lyrics[lyricCount].text        = text;
        lyrics[lyricCount].wordOffset  = -1;
        lyrics[lyricCount].wordCount   = 0;
        lyricCount++;
    }
    Serial.println("[SpotifyLyrics] LINE_SYNCED: " + String(lyricCount) + " lines");
    return lyricCount > 0;
}

static bool parseWordSynced(JsonArray lines) {
    int    lineWordStart = 0;
    int    lineWordCount = 0;
    long   lineStartMs   = 0;
    String lineText      = "";
    long   prevWordMs    = -9999L;
    bool   firstWord     = true;

    for (JsonObject entry : lines) {
        const char* msStr = entry["startTimeMs"] | "0";
        const char* txt   = entry["words"]       | "";
        String wordStr(txt);
        wordStr.trim();
        if (wordStr.isEmpty() || wordStr == "\u266a") continue;

        long ms = atol(msStr);

        // Debug: print first 5 entries so we can verify format
        if (wordTimestampCount < 5) {
            Serial.println("[WSYNCED] " + String(ms) + "ms: \"" + wordStr + "\"");
        }

        bool newLine = firstWord
                       || (ms - prevWordMs > 1500)
                       || (lineWordCount >= 6);

        if (newLine && !firstWord) {
            // Flush current line
            if (lyricCount < MAX_LYRIC_LINES) {
                lyrics[lyricCount].timestampMs = lineStartMs;
                lyrics[lyricCount].text        = lineText;
                lyrics[lyricCount].wordOffset  = lineWordStart;
                lyrics[lyricCount].wordCount   = lineWordCount;
                lyricCount++;
            }
            lineWordStart = wordTimestampCount;
            lineWordCount = 0;
            lineText      = "";
        }

        if (newLine) lineStartMs = ms;
        firstWord = false;

        if (wordTimestampCount < MAX_WORD_ENTRIES)
            wordStartMs[wordTimestampCount++] = ms;

        if (lineText.length() > 0) lineText += " ";
        lineText      += wordStr;
        lineWordCount++;
        prevWordMs     = ms;
    }

    // Flush last line
    if (lineWordCount > 0 && lyricCount < MAX_LYRIC_LINES) {
        lyrics[lyricCount].timestampMs = lineStartMs;
        lyrics[lyricCount].text        = lineText;
        lyrics[lyricCount].wordOffset  = lineWordStart;
        lyrics[lyricCount].wordCount   = lineWordCount;
        lyricCount++;
    }

    Serial.println("[SpotifyLyrics] WORD_SYNCED: " + String(lyricCount)
                   + " lines, " + String(wordTimestampCount) + " words");
    return lyricCount > 0;
}

// ── Public ────────────────────────────────────────────────────────────────────

bool spotify_getLyrics(const String &trackId) {
    if (accessToken == "") {
        Serial.println("[SpotifyLyrics] No access token");
        return false;
    }

    HTTPClient http;
    WiFiClientSecure client;
    client.setInsecure();

    String url = "https://spclient.wg.spotify.com/color-lyrics/v2/track/"
                 + trackId + "?format=json&market=from_token";

    http.begin(client, url);
    http.addHeader("Authorization", "Bearer " + accessToken);
    http.addHeader("App-Platform", "WebPlayer");
    http.setTimeout(8000);

    int code = http.GET();
    if (code != 200) {
        Serial.println("[SpotifyLyrics] HTTP " + String(code));
        http.end();
        return false;
    }

    // Filter keeps only the fields we need — avoids parsing the full payload
    JsonDocument filter;
    filter["lyrics"]["syncType"]             = true;
    filter["lyrics"]["lines"][0]["startTimeMs"] = true;
    filter["lyrics"]["lines"][0]["wordStrs"]       = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, *http.getStreamPtr(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        Serial.println("[SpotifyLyrics] Parse error: " + String(err.c_str()));
        return false;
    }

    const char* syncType = doc["lyrics"]["syncType"] | "NONE";
    Serial.println("[SpotifyLyrics] syncType: " + String(syncType));

    lyrics_clear();
    JsonArray lines = doc["lyrics"]["lines"].as<JsonArray>();

    if (String(syncType) == "WORD_SYNCED") return parseWordSynced(lines);
    if (String(syncType) == "LINE_SYNCED") return parseLineSynced(lines);

    return false;
}