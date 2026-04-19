#include "get_lyrics.h"
#include "config.h"
#include "net.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

LyricLine lyrics[MAX_LYRIC_LINES];
int       lyricCount = 0;

long wordStartMs[MAX_WORD_ENTRIES];
int  wordTimestampCount = 0;

void lyrics_clear() {
    lyricCount          = 0;
    wordTimestampCount  = 0;
    for (int i = 0; i < MAX_LYRIC_LINES; i++) {
        lyrics[i].timestampMs = 0;
        lyrics[i].text        = "";
        lyrics[i].wordOffset  = -1;
        lyrics[i].wordCount   = 0;
    }
}

// Parse [mm:ss.xx] into milliseconds
static long parseTimestamp(const String &ts) {
    // format: mm:ss.xx
    int colonPos = ts.indexOf(':');
    int dotPos   = ts.indexOf('.');
    if (colonPos < 0 || dotPos < 0) return -1;

    long minutes = ts.substring(0, colonPos).toInt();
    long seconds = ts.substring(colonPos + 1, dotPos).toInt();
    long centiseconds = ts.substring(dotPos + 1).toInt();

    return (minutes * 60000) + (seconds * 1000) + (centiseconds * 10);
}

static String urlEncode(const String &s) {
    String out;
    out.reserve(s.length() * 2);
    for (int i = 0; i < (int)s.length(); i++) {
        uint8_t c = (uint8_t)s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

static WiFiClientSecure lrclibClient;
static bool             lrclibClientReady = false;

bool lyrics_fetch(const String &title, const String &artist) {
    if (!wifi_connected()) {
        Serial.println("[Lyrics] WiFi not connected");
        return false;
    }

    lyrics_clear();

    if (!lrclibClientReady) {
        lrclibClient.setInsecure();
        lrclibClient.setHandshakeTimeout(4);
        lrclibClientReady = true;
    }

    String url = "https://lrclib.net/api/get?track_name="
                 + urlEncode(title) + "&artist_name=" + urlEncode(artist);

    HTTPClient http;
    http.begin(lrclibClient, url);
    http.setTimeout(4000);
    http.addHeader("User-Agent", "karaoke-esp32/1.0");
    int code = http.GET();

    if (code != 200) {
        Serial.println("[Lyrics] Fetch failed, HTTP " + String(code));
        http.end();
        lrclibClient.stop();  // force fresh handshake on next attempt
        return false;
    }

    String payload = http.getString();
    http.end();
    // don't stop lrclibClient — keep connection alive for reuse

    int lrcStart = payload.indexOf("\"syncedLyrics\":");
    if (lrcStart < 0) { Serial.println("[Lyrics] No syncedLyrics field"); return false; }
    lrcStart = payload.indexOf('"', lrcStart + 15);
    if (lrcStart < 0) { Serial.println("[Lyrics] No opening quote"); return false; }
    lrcStart++;  // skip opening quote

    int lrcEnd = lrcStart;
    while (lrcEnd < (int)payload.length()) {
        if (payload[lrcEnd] == '\\') { lrcEnd += 2; continue; }
        if (payload[lrcEnd] == '"')  break;
        lrcEnd++;
    }

    if (lrcEnd >= (int)payload.length()) { Serial.println("[Lyrics] No closing quote"); return false; }

    String lrc = payload.substring(lrcStart, lrcEnd);
    lrc.replace("\\n", "\n");

    // Parse each line
    int pos = 0;
    while (pos < (int)lrc.length() && lyricCount < MAX_LYRIC_LINES) {
        int lineEnd = lrc.indexOf('\n', pos);
        if (lineEnd < 0) lineEnd = lrc.length();

        String line = lrc.substring(pos, lineEnd);
        line.trim();

        if (line.length() > 2 && line.charAt(0) == '[') {
            int closeBracket = line.indexOf(']');
            if (closeBracket > 0) {
                String ts   = line.substring(1, closeBracket);
                String text = line.substring(closeBracket + 1);
                text.trim();

                long ms = parseTimestamp(ts);
                if (ms >= 0) {
                    lyrics[lyricCount].timestampMs = ms;
                    lyrics[lyricCount].text = text;
                    lyricCount++;
                }
            }
        }

        pos = lineEnd + 1;
    }

    Serial.println("[Lyrics] Fetched " + String(lyricCount) + " lines");
    return lyricCount > 0;
}

void lyrics_printAll() {
    for (int i = 0; i < lyricCount; i++) {
        Serial.println("[" + String(lyrics[i].timestampMs) + "ms] "
                       + lyrics[i].text);
    }
}

