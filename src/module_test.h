#pragma once

// Comment out to disable, uncomment to enable
// ─────────────────────────────────────────────
#define WIFI_TEST
#define DISPLAY_TEST
//#define DAC_TEST
//#define RTC_TEST
#define SPOTIFY_TEST
#define LRCLIB_TEST
//#define LYRIC_SYNC_TEST
//#define FULL_SYSTEM_TEST
// ─────────────────────────────────────────────

void Module_Test_Init(void);
void Module_Test_Run(void);

#ifdef WIFI_TEST
void WiFi_Test(void);
#endif

#ifdef DISPLAY_TEST
void Display_Test(void);
#endif

#ifdef DAC_TEST
void DAC_Test(void);
#endif

#ifdef RTC_TEST
void RTC_Test(void);
#endif

#ifdef SPOTIFY_TEST
void Spotify_Test(void);
#endif

#ifdef LRCLIB_TEST
void LRCLib_Test(void);
#endif

#ifdef LYRIC_SYNC_TEST
void LyricSync_Test(void);
#endif

#ifdef FULL_SYSTEM_TEST
void FullSystem_Test(void);
#endif