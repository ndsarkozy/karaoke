#pragma once

// Comment out to disable, uncomment to enable
// ─────────────────────────────────────────────
//#define DISPLAY_TEST
// ─────────────────────────────────────────────

void Module_Test_Init(void);
void Module_Test_Run(void);

#ifdef DISPLAY_TEST
void Display_Test(void);
#endif
