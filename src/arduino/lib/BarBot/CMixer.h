#ifndef CMIXER_H
#define CMIXER_H

#define MIXER_IDLE_POSITION       140
#define MIXER_DISPENSE_POSITION    65
#define MIXER_DELAY              1000  // How long to wait after dispensing before being "done"
#define MIXER_DETTACH_TIME       1000  // how long to wait after dispensing before dettaching

#include "CMixer.h"
#include "CDispenser.h"

#include "../../../../libraries/Servo/Servo.h"

#include "Arduino.h"
#include <avr/pgmspace.h>

class CMixer : public CDispenser
{
  public:
     CMixer(uint8_t servo_pin);
     ~CMixer();
     uint8_t          get_dispener_type();
     bool             dispense(uint16_t qty);
     bool             loop();
     dispenser_state  get_status();
     void             stop();
     void             move_to_idle();
     void             move_to_dispense();
     
  private:
    Servo _servo;
    unsigned long long _dispense_start;
    uint16_t _dispense_time; // how long in ms to dispense for
    bool _attached;
    uint8_t  _servo_pin;
    bool _dispensed;
  
};

#endif
