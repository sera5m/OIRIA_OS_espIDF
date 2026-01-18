




#include <math.h>
#include <stdbool.h>

#include "driver/pulse_cnt.h"
#include "drivers/encoders/ky040_driver.c"
#include "types.h"




#define key_enter 0x23CE 
// ⏎ 
#define key_back 0x232B 
// ⌫ 
#define key_up 0x2191 
// ↑ 
#define key_down 0x2193 
// ↓ 
#define key_left 0x2190 
// ←
 #define key_right 0x2192 
 // →

#define FixDoubleInputGlitch 1 //encoder makes two pulses for notch
 extern QueueHandle_t ProcInputQueTarget;



typedef struct encoder_state { pcnt_unit_handle_t unit; int16_t last_count; } encoder_state_t;

typedef struct encoder_state encoder_state_t;

void inphandler_encoder_delta(uint8_t id, int32_t delta) {
	
	
	
    // semantic layer decides what delta means
}

void inphandler_button_event(uint8_t id, bool pressed) {
    // semantic layer decides meaning
}
