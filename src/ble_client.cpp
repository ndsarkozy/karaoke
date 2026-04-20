#include "ble_client.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define PROGRESS_CHAR_UUID  "12345678-1234-1234-1234-123456789ab1"
#define LYRICS_CHAR_UUID    "12345678-1234-1234-1234-123456789ab2"

static BLEClient*               bleClient    = nullptr;
static BLERemoteCharacteristic* progressChar = nullptr;
static BLERemoteCharacteristic* lyricsChar   = nullptr;
static volatile bool connected   = false;
static bool newLyrics            = false;
static bool newProgress          = false;

static long   progressMs         = 0;
static bool   isPlaying          = false;
static String assembledLyrics    = "";

static String chunkBuffer        = "";
static int    lastChunkIndex     = -1;
static int    totalChunks        = 0;

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {
        connected = true;
        Serial.println("[BLE] Connected");
    }
    void onDisconnect(BLEClient*) override {
        connected = false;
        progressChar = nullptr;
        lyricsChar   = nullptr;
        Serial.println("[BLE] Disconnected");
    }
};
static ClientCallbacks clientCallbacks;

static void onProgressNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (len < 9) return;
    isPlaying  = data[0] == 1;
    progressMs = 0;
    for (int i = 0; i < 8; i++) progressMs |= ((long)data[i + 1] << (i * 8));
    newProgress = true;
}

static void onLyricsNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    int colonPos = -1;
    for (int i = 0; i < (int)len; i++) {
        if (data[i] == ':') { colonPos = i; break; }
    }
    if (colonPos < 0) return;

    String header = String((char*)data).substring(0, colonPos);
    String chunk  = "";
    for (int i = colonPos + 1; i < (int)len; i++) chunk += (char)data[i];

    int slashPos = header.indexOf('/');
    if (slashPos < 0) return;

    int chunkIdx = header.substring(0, slashPos).toInt();
    int total    = header.substring(slashPos + 1).toInt();

    Serial.printf("[BLE] Chunk %d/%d len=%d\n", chunkIdx, total, (int)chunk.length());

    if (chunkIdx == 0) {
        chunkBuffer    = chunk;
        totalChunks    = total;
        lastChunkIndex = 0;
    } else if (chunkIdx == lastChunkIndex + 1) {
        chunkBuffer   += chunk;
        lastChunkIndex = chunkIdx;
    } else {
        Serial.println("[BLE] Out of order chunk, resetting");
        chunkBuffer    = "";
        lastChunkIndex = -1;
        return;
    }

    if (chunkIdx == total - 1) {
        Serial.printf("[BLE] All chunks received, total length=%d\n", chunkBuffer.length());
        assembledLyrics = chunkBuffer;
        newLyrics       = true;
    }
}

static bool connectToServer() {
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults results = scan->start(5, false);

    BLEAdvertisedDevice* target = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice d = results.getDevice(i);
        if (d.haveServiceUUID() && d.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            target = new BLEAdvertisedDevice(d);
            break;
        }
    }
    if (!target) return false;

    if (bleClient) {
        bleClient->disconnect();
        bleClient = nullptr;
    }

    bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(&clientCallbacks);

    if (!bleClient->connect(target)) {
        delete target;
        return false;
    }

    bleClient->setMTU(512);
    delay(500);

    BLERemoteService* svc = bleClient->getService(SERVICE_UUID);
    if (!svc) { bleClient->disconnect(); delete target; return false; }

    progressChar = svc->getCharacteristic(PROGRESS_CHAR_UUID);
    lyricsChar   = svc->getCharacteristic(LYRICS_CHAR_UUID);

    if (!progressChar || !lyricsChar) {
        bleClient->disconnect();
        delete target;
        return false;
    }

    progressChar->registerForNotify(onProgressNotify);
    lyricsChar->registerForNotify(onLyricsNotify);

    delete target;
    return true;
}

void ble_init() {
    BLEDevice::init("KaraokeESP");
}

bool ble_isConnected()         { return connected; }
bool ble_newLyricsAvailable()  { if (newLyrics)   { newLyrics   = false; return true; } return false; }
bool ble_newProgressAvailable(){ if (newProgress) { newProgress = false; return true; } return false; }
long   ble_getProgressMs()     { return progressMs; }
bool   ble_getIsPlaying()      { return isPlaying; }
String ble_getLyrics()         { return assembledLyrics; }

void ble_task(void*) {
    ble_init();
    while (true) {
        if (!connected) {
            Serial.println("[BLE] Scanning...");
            if (!connectToServer()) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
