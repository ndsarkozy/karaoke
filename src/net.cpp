#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <Arduino.h>

void wifi_connect() {
    Serial.println("[WiFi] Connecting to " + String(WIFI_SSID));
    WiFi.mode(WIFI_STA);
    // Use Google + Cloudflare DNS instead of router DNS to avoid resolution failures
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE,
                IPAddress(8, 8, 8, 8), IPAddress(1, 1, 1, 1));
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WiFi] Connected");
        Serial.println("[WiFi] IP:   " + WiFi.localIP().toString());
        Serial.println("[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
    } else {
        Serial.println("[WiFi] FAILED. Check WIFI_SSID and WIFI_PASS in config.h");
    }
}

bool wifi_connected() {
    return WiFi.status() == WL_CONNECTED;
}

void wifi_check() {
    if (!wifi_connected()) {
        Serial.println("[WiFi] Connection lost. Reconnecting...");
        WiFi.disconnect();
        delay(1000);
        wifi_connect();
    }
}