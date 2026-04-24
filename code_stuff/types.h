//ligma
#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
//#include <stdint.h>
//#include <cmath.h>
#pragma once

//pragma twice would be funny lol

//math

#define ui8 uint8_t
#define ui16 uint16_t

struct int16vect {
    int16_t x;
    int16_t y;
    int16_t z;

#ifdef __cplusplus
    int16vect() : x(0), y(0), z(0) {}
    int16vect(int16_t x, int16_t y, int16_t z) : x(x), y(y), z(z) {}
#endif
};

struct vec2_ui16t {
    int16_t x;
    int16_t y;

#ifdef __cplusplus
    vec2_ui16t() : x(0), y(0) {}
    vec2_ui16t(int16_t x, int16_t y) : x(x), y(y) {}
#endif
};



typedef struct {
        uint8_t width;
        uint8_t height;
        uint16_t *data;   // RGB565 pixels
    } s_bmp_t;
//16 bit bitmap description

//application info-modes-full type implementations of whatever in types.cpp

//enums

typedef enum{HMM_BIOMONITOR, //Current fuckshit like that beep boop beeip in hopital
HMM_DAYHISTORY,    //a bar graph over the past x days.
   HMM_HISTORY, //this month/historical trends. on long scales of time we'll just store average hr as waking/sleeping 
   HMM_SETTINGS   //idk man what do you even config here?
}HealthmonitorMode;

typedef enum {
    EDIT_OFF,        // Normal display mode
    EDIT_RUNNING,    // Actively editing values
    EDIT_CONFIRM     // Save/cancel prompt
} EditState;

typedef enum {
    GSLC_POWER,         // sleep modes
    GSLC_ALERTS,        // notifications, alarms
    GSLC_DISPLAY,       // screen settings
    GSLC_DATA,          // storage, sd card
    GSLC_WIRELESS,      // wifi, bt
    GSLC_EXT_HARDWARE,  // modules, sensors
    GSLC_CATEGORY_COUNT
} GlobalSettingsListCategory;


typedef enum {
    ALERT_SRC_PHONE,
    ALERT_SRC_MISC_INTERNAL,
    ALERT_SRC_CLOCK
} AlertSource;



typedef struct {
    uint8_t intensity;  // 0-255 maybe
    int flash_light;    // bool
    AlertSource source;
} AlertsSettings;

typedef struct __attribute__((packed)) {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	}s_bounds_16u;

/*
typedef struct {
    int nfc_enabled;  // bool
    int wifi_enabled; // bool
    int bt_enabled;   // bool
} WirelessSettings;*/

typedef struct {
    uint32_t storage_used_mb;
    uint32_t storage_total_mb;
} DataSettings;



typedef enum {//types.h, app names in the list
  APP_LOCK_SCREEN,
  APP_HEALTH,
  APP_NFC,
  APP_SETTINGS,
  APP_GYRO_INFO,
  APP_FILES,
  APP_RADIO,
  APP_IR_REMOTE,
  APP_UTILITIES,
  APP_ETOOLS,
  APP_RUBBERDUCKY,
  APP_CONNECTIONS,
  APP_SMART_DEVICES,
  APP_DIAGNOSTICS,
  APP_GAMES, //things like snake, i think
  APP_COUNT // always handy for bounds checking
}AppName;

typedef enum{
nam_reading,
nam_writing,
nam_sleep,
nam_saving,
nam_loadfromStorage,
}nfcAppMode;



extern const char* GlobalSettingsListCategoryNames[];

extern const char* appNames[];

extern const char* GameNames[];


typedef enum{snake,snakeys,pong,tetris,evilTetris,asteroids,_2048,poker,chess,checkers,dice,cat,idk}APP_GAMES_GAMELIST;
//reminder to self that 2048 is by default understood as an int litteral not a name, so use _2048
//the temptation to say kibby here is immense, but i'd likely spell it correctly and be forced to debug it, agony....OF NOT TYPING KIBBY
// ^   ^    __
//( 0.0 ) _//-
//| > < |/ /
//d_____b//
//wow i'm bad at ascii art
extern const char* GameApps[]; 
extern const char* GameDescriptions[];
















#endif // TYPES_H