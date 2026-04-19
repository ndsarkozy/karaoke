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
    http.setTimeout(6000);
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

static WiFiClientSecure nowPlayingClient;
static HTTPClient       nowPlayingHttp;
static bool             nowPlayingReady = false;

void spotify_closeConnection() {
    nowPlayingHttp.end();
    nowPlayingClient.stop();
}

bool spotify_getNowPlaying(SpotifyTrack &track) {
    if (accessToken == "") {
        Serial.println("[Spotify] No access token");
        return false;
    }

    if (!nowPlayingReady) {
        nowPlayingClient.setInsecure();
        nowPlayingReady = true;
    }

    nowPlayingHttp.setReuse(true);
    nowPlayingHttp.begin(nowPlayingClient, "https://api.spotify.com/v1/me/player/currently-playing");
    nowPlayingHttp.setTimeout(5000);
    nowPlayingHttp.addHeader("Authorization", "Bearer " + accessToken);

    int code = nowPlayingHttp.GET();

    if (code == 204) {
        Serial.println("[Spotify] Nothing playing");
        nowPlayingHttp.end();
        return false;
    }

    if (code != 200) {
        Serial.println("[Spotify] Now playing failed, HTTP " + String(code));
        nowPlayingHttp.end();
        if (code < 0) {
            // SSL error — drop connection so next call does a fresh handshake
            nowPlayingClient.stop();
        }
        return false;
    }

    String payload = nowPlayingHttp.getString();
    nowPlayingHttp.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) {
        Serial.println("[Spotify] Now playing JSON parse error");
        return false;
    }

    track.title       = doc["item"]["name"].as<String>();
    track.artist      = doc["item"]["artists"][0]["name"].as<String>();
    track.trackId     = doc["item"]["id"].as<String>();
    track.albumArtUrl = doc["item"]["album"]["images"][1]["url"].as<String>(); // 300x300
    track.progressMs  = doc["progress_ms"].as<long>();
    track.durationMs  = doc["item"]["duration_ms"].as<long>();
    track.isPlaying   = doc["is_playing"].as<bool>();

    Serial.println("[Spotify] Track: " + track.title);
    Serial.println("[Spotify] Artist: " + track.artist);
    Serial.println("[Spotify] Progress: " + String(track.progressMs / 1000) + "s");

    return true;
}