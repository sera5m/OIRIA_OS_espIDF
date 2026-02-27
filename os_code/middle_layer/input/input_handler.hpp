




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



typedef enum {pulse,lift,down,positionDelta,analogInput,null}keyaction;
//buttons go down and up, but other things like twist on here make pulses. 
//i2c periphrials may be used for analog input. 



typedef struct {
    uint16_t key;
    uint16_t proc_destination_id;
    input_event_type keyaction; 
} Q_input_event_t;