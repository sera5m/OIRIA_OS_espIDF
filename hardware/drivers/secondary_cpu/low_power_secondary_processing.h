/*
 * low_power_secondary_processing.h
 *
 *  Created on: Jan 12, 2026
 *      Author: dev
 */




// Tell the secondary chip:
//  - ESP is going to sleep
//  - start / continue sensor capture
#include <stddef.h>
#include <math.h>
#include <stdint.h>


typedef enum{prs_nfc,prs_step_up_5vrail,prs_stepdown_ir_1dot8v,prs_idk}prs_hardware_regs;

void lpsp_punchin(void);//intended to be a wakeup for this cpu, booting it and getting it ready for it's sleepy reading of stuff

// Called after wake
// Returns number of bytes written into buffer
size_t lpsp_retrieve_data(uint8_t *out_buf, size_t max_len);

void lpsp_select_power(uint8_t bitmask_prs_hardware_regs);
 //0x abcd where a=nfc,b=psu_5v,c=stepdown,d=undetermined, easy 1 to one, where 0x1000 and 0 turns that off, 1 turns it on
 

