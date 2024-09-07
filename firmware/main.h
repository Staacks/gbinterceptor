#ifndef GBINTERCEPTOR_MAIN
#define GBINTERCEPTOR_MAIN

#define GBSENSE_PIN 0

#define LED_SWITCH_PIN 1
#define LED_PIN_MASK 0x02

#define BASEVERSION "1.2.0"

#ifdef BASE_VIDEO_MODE
#define VERSION BASEVERSION "B"
#else
#define VERSION BASEVERSION
#endif

//On-screen display
#define MODE_INFO_DURATION 100 //Duration of the mode info in frames
#define GAME_DETECTED_INFO_DURATION 200 //Duration of the mode info in frames

#endif