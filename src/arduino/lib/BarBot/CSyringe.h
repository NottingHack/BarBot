#ifndef CSYRINGE_H
#define CSYRINGE_H

#define SYRINGE_WAIT_TIME  100     // How long to wait after dispensing before sucking
#define SYRINGE_SUCK_TIME  300     // After dispensing, suck for this many milliseconds
#define SYRINGE_DRIP_TIME 5000     // How long to wait after dispensing before being "done"

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
    enum syringe_state
    {
      READY,
      DISPENSING,
      SUCK_WAIT,
      SUCKING,
      DRIP_WAIT
    };

    unsigned long long _dispense_start;
    uint16_t _dispense_time; // how long in ms to dispense for
    uint8_t _suck_pin;
    uint8_t _squirt_pin;
    syringe_state _syr_state;
};

#endif
