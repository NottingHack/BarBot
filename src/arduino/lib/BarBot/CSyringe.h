#ifndef CSYRINGE_H
#define CSYRINGE_H

#define SYRINGE_SUCK_TIME 100     // After dispensing, suck for this many milliseconds

#include "CSyringe.h"
#include "CDispenser.h"

#include "Arduino.h"
#include <avr/pgmspace.h>

class CSyringe : public CDispenser
{
  public:
     CSyringe(uint8_t suck_pin, uint8_t squirt_pin);
     ~CSyringe();
     uint8_t          get_dispener_type();
     bool             dispense(uint16_t qty);
     bool             loop();
     dispenser_state  get_status();
     void             stop();
     
  private:
    unsigned long long _dispense_start;
    uint16_t _dispense_time; // how long in ms to dispense for
    uint8_t _suck_pin;
    uint8_t _squirt_pin;
    bool _squirt_done;
};

#endif
