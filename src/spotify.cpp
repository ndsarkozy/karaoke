#include "spotify.h"
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