#include "ble_client.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define PROGRESS_CHAR_UUID  "12345678-1234-1234-1234-123456789ab1"
#define LYRICS_CHAR_UUID    "12345678-1234-1234-1234-123456789ab2"
#define ALBUM_CHAR_UUID     "12345678-1234-1234-1234-123456789ab3"

#define ALBUM_BUF_SIZE 26000

static BLEClient*               bleClient    = nullptr;
static BLERemoteCharacteristic* progressChar = nullptr;
static BLERemoteCharacteristic* lyricsChar   = nullptr;
static BLERemoteCharacteristic* albumChar    = nullptr;

static volatile bool connected   = false;
static bool newLyrics            = false;
static bool newProgress          = false;
static bool newAlbum             = false;

static long   progressMs         = 0;
static long   durationMs         = 0;
static bool   isPlaying          = false;

// Lyric chunk reassembly — static buffer avoids heap fragmentation
#define LYRIC_BUF_SIZE 10240
static char   lyricChunkBuf[LYRIC_BUF_SIZE];
static size_t lyricAssembled  = 0;
static int    lastChunkIndex  = -1;

// Album chunk reassembly
static uint8_t albumBuf[ALBUM_BUF_SIZE];
static size_t  albumLen          = 0;
static uint8_t albumChunkBuf[ALBUM_BUF_SIZE];
static size_t  albumAssembled    = 0;
static int     albumLastChunk    = -1;

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient*) override {
        connected = true;
        Serial.println("[BLE] Connected");
    }
    void onDisconnect(BLEClient*) override {
        connected    = false;
        progressChar = nullptr;
        lyricsChar   = nullptr;
        albumChar    = nullptr;
        Serial.println("[BLE] Disconnected");
    }
};
static ClientCallbacks clientCallbacks;

// ── Notification callbacks ────────────────────────────────────────────────────
static void onProgressNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    if (len < 9) return;
    isPlaying  = data[0] == 1;
    progressMs = 0;
    for (int i = 0; i < 8; i++) progressMs |= ((long)data[i+1] << (i*8));
    if (len >= 17) {
        durationMs = 0;
        for (int i = 0; i < 8; i++) durationMs |= ((long)data[i+9] << (i*8));
    }
    newProgress = true;
}

static void onLyricsNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    int colonPos = -1;
    for (int i = 0; i < (int)len; i++) { if (data[i] == ':') { colonPos = i; break; } }
    if (colonPos < 0 || colonPos >= 20) return;

    char hbuf[21];
    memcpy(hbuf, data, colonPos);
    hbuf[colonPos] = '\0';
    String header = String(hbuf);

    int    payloadStart = colonPos + 1;
    size_t payloadLen   = (payloadStart < (int)len) ? len - payloadStart : 0;

    int slashPos = header.indexOf('/');
    if (slashPos < 0) return;
    int chunkIdx = header.substring(0, slashPos).toInt();
    int total    = header.substring(slashPos+1).toInt();

    Serial.printf("[BLE] Lyric chunk %d/%d\n", chunkIdx, total);

    if (chunkIdx == 0) {
        lyricAssembled = 0;
        lastChunkIndex = 0;
    } else if (chunkIdx == lastChunkIndex + 1) {
        lastChunkIndex = chunkIdx;
    } else {
        lyricAssembled = 0; lastChunkIndex = -1; return;
    }

    if (payloadLen > 0 && lyricAssembled + payloadLen < LYRIC_BUF_SIZE) {
        memcpy(lyricChunkBuf + lyricAssembled, data + payloadStart, payloadLen);
        lyricAssembled += payloadLen;
    }

    if (chunkIdx == total - 1) {
        lyricChunkBuf[lyricAssembled] = '\0';
        newLyrics = true;   // main loop reads lyricChunkBuf via ble_getLyrics()
        Serial.printf("[BLE] Lyrics assembled: %d bytes\n", (int)lyricAssembled);
    }
}

static void onAlbumNotify(BLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    // Header: "chunkIdx/total:" then raw JPEG bytes
    int colonPos = -1;
    for (int i = 0; i < (int)len && i < 20; i++) { if (data[i] == ':') { colonPos = i; break; } }
    if (colonPos < 0) return;

    char hbuf[21];
    memcpy(hbuf, data, colonPos);
    hbuf[colonPos] = '\0';
    String header   = String(hbuf);
    int    payloadStart = colonPos + 1;
    size_t payloadLen   = len - payloadStart;

    int slashPos = header.indexOf('/');
    if (slashPos < 0) return;
    int chunkIdx = header.substring(0, slashPos).toInt();
    int total    = header.substring(slashPos+1).toInt();

    Serial.printf("[BLE] Album chunk %d/%d (%d bytes)\n", chunkIdx, total, (int)payloadLen);

    if (chunkIdx == 0) {
        albumAssembled = 0;
        albumLastChunk = 0;
    } else if (chunkIdx != albumLastChunk+1) {
        albumAssembled = 0; albumLastChunk = -1; return;
    } else {
        albumLastChunk = chunkIdx;
    }

    if (albumAssembled + payloadLen <= ALBUM_BUF_SIZE) {
        memcpy(albumChunkBuf + albumAssembled, data + payloadStart, payloadLen);
        albumAssembled += payloadLen;
    }

    if (chunkIdx == total-1) {
        memcpy(albumBuf, albumChunkBuf, albumAssembled);
        albumLen = albumAssembled;
        newAlbum = true;
        Serial.printf("[BLE] Album assembled: %d bytes\n", (int)albumLen);
    }
}

// ── Connection ────────────────────────────────────────────────────────────────
static bool connectToServer() {
    BLEScan* scan = BLEDevice::getScan();
    scan->setActiveScan(true);
    BLEScanResults results = scan->start(5, false);

    BLEAdvertisedDevice* target = nullptr;
    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice d = results.getDevice(i);
        if (d.haveServiceUUID() && d.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            target = new BLEAdvertisedDevice(d); break;
        }
    }
    if (!target) return false;

    if (bleClient) { bleClient->disconnect(); bleClient = nullptr; }
    bleClient = BLEDevice::createClient();
    bleClient->setClientCallbacks(&clientCallbacks);

    if (!bleClient->connect(target)) { delete target; return false; }
    bleClient->setMTU(512);
    delay(500);

    BLERemoteService* svc = bleClient->getService(SERVICE_UUID);
    if (!svc) { bleClient->disconnect(); delete target; return false; }

    progressChar = svc->getCharacteristic(PROGRESS_CHAR_UUID);
    lyricsChar   = svc->getCharacteristic(LYRICS_CHAR_UUID);
    albumChar    = svc->getCharacteristic(ALBUM_CHAR_UUID);

    if (!progressChar || !lyricsChar) { bleClient->disconnect(); delete target; return false; }

    progressChar->registerForNotify(onProgressNotify);
    lyricsChar->registerForNotify(onLyricsNotify);
    if (albumChar) albumChar->registerForNotify(onAlbumNotify);

    delete target;
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────
void   ble_init()            { BLEDevice::init("KaraokeESP"); }
bool   ble_isConnected()     { return connected; }
bool   ble_getIsPlaying()    { return isPlaying; }
long   ble_getProgressMs()   { return progressMs; }
long   ble_getDurationMs()   { return durationMs; }
const char* ble_getLyrics()  { return lyricChunkBuf; }
const uint8_t* ble_getAlbumBuf() { return albumBuf; }
size_t         ble_getAlbumLen() { return albumLen;  }

bool ble_newLyricsAvailable()  { if (newLyrics)   { newLyrics   = false; return true; } return false; }
bool ble_newProgressAvailable(){ if (newProgress) { newProgress = false; return true; } return false; }
bool ble_newAlbumAvailable()   { if (newAlbum)    { newAlbum    = false; return true; } return false; }

void ble_task(void*) {
    ble_init();
    while (true) {
        if (!connected) {
            Serial.println("[BLE] Scanning...");
            if (!connectToServer()) vTaskDelay(pdMS_TO_TICKS(3000));
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
