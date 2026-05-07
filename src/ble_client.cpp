#include "ble_client.h"
#include <BLEDevice.h>
#include <BLEClient.h>
#include <BLEScan.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define PROGRESS_CHAR_UUID  "12345678-1234-1234-1234-123456789ab1"
#define LYRICS_CHAR_UUID    "12345678-1234-1234-1234-123456789ab2"
#define ALBUM_CHAR_UUID     "12345678-1234-1234-1234-123456789ab3"

#define ALBUM_BUF_SIZE 20000

static BLEClient*               bleClient    = nullptr;
static BLERemoteCharacteristic* progressChar = nullptr;
static BLERemoteCharacteristic* lyricsChar   = nullptr;
static BLERemoteCharacteristic* albumChar    = nullptr;

static volatile bool connected   = false;
static volatile bool newLyrics   = false;
static volatile bool newProgress = false;
static volatile bool newAlbum    = false;

static long   progressMs         = 0;
static long   durationMs         = 0;
static bool   isPlaying          = false;

// Lyric chunk reassembly — static buffer avoids heap fragmentation
#define LYRIC_BUF_SIZE 8192
static char   lyricChunkBuf[LYRIC_BUF_SIZE];
static size_t lyricAssembled  = 0;
static int    lastChunkIndex  = -1;

// Album chunk reassembly — single buffer (chunks land directly here).
// `albumLen` is only published on the last chunk, so consumers reading
// (albumBuf, albumLen) never see a partial image. Main task must call
// ble_lockAlbum() while it's reading albumBuf so the BLE callback can't
// overwrite mid-decode.
static uint8_t albumBuf[ALBUM_BUF_SIZE];
static size_t  albumLen          = 0;
static size_t  albumAssembled    = 0;
static int     albumLastChunk    = -1;
static volatile bool albumLocked = false;

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

    int    payloadStart = colonPos + 1;
    size_t payloadLen   = (payloadStart < (int)len) ? len - payloadStart : 0;

    // Avoid heap allocation inside BLE callback — use C string ops directly.
    char* slash = strchr(hbuf, '/');
    if (!slash) return;
    *slash = '\0';
    int chunkIdx = atoi(hbuf);
    int total    = atoi(slash + 1);

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
        newLyrics = true;
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
    int    payloadStart = colonPos + 1;
    size_t payloadLen   = len - payloadStart;

    // Avoid heap allocation inside BLE callback — use C string ops directly.
    char* slash = strchr(hbuf, '/');
    if (!slash) return;
    *slash = '\0';
    int chunkIdx = atoi(hbuf);
    int total    = atoi(slash + 1);

    // Drop any new transfer while the main task is decoding the current one.
    // Sender will retry on the next track-change. Without this, the JPEG
    // bytes get clobbered mid-decode and the image renders jumbled.
    if (albumLocked) {
        if (chunkIdx == 0) albumLastChunk = -1;
        return;
    }

    if (chunkIdx == 0) {
        albumAssembled = 0;
        albumLastChunk = 0;
    } else if (chunkIdx != albumLastChunk+1) {
        albumAssembled = 0; albumLastChunk = -1; return;
    } else {
        albumLastChunk = chunkIdx;
    }

    if (albumAssembled + payloadLen <= ALBUM_BUF_SIZE) {
        memcpy(albumBuf + albumAssembled, data + payloadStart, payloadLen);
        albumAssembled += payloadLen;
    }

    if (chunkIdx == total-1) {
        albumLen = albumAssembled;
        newAlbum = true;
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
    delay(500);  // let GATT service discovery complete before accessing services

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
void           ble_lockAlbum()   { albumLocked = true;  }
void           ble_unlockAlbum() { albumLocked = false; }

bool ble_newLyricsAvailable()  { if (newLyrics)   { newLyrics   = false; return true; } return false; }
bool ble_newProgressAvailable(){ if (newProgress) { newProgress = false; return true; } return false; }
bool ble_newAlbumAvailable()   { if (newAlbum)    { newAlbum    = false; return true; } return false; }

void ble_task(void*) {
    ble_init();
    while (true) {
        if (!connected) {
            Serial.println("[BLE] Scanning...");
            if (!connectToServer()) vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
