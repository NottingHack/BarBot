#include "CSyringe.h"

/* mixer dispenser - qty is ml to dispense */

CSyringe::CSyringe(uint8_t suck_pin, uint8_t squirt_pin)
{

  _last_used = millis();
  _state = CSyringe::IDLE;
  _suck_pin = suck_pin;
  _squirt_pin = squirt_pin;
  analogWrite(_suck_pin,0);
  analogWrite(_squirt_pin,0);
  _syr_state = READY;
}

CSyringe::~CSyringe()
{
  analogWrite(_suck_pin,0);
  analogWrite(_squirt_pin,0);
}

uint8_t CSyringe::get_dispener_type()
{
  return DISPENSER_SYRINGE;
}

// qty = milliseconds to dispense for
bool CSyringe::dispense(uint16_t qty)
{
  if (_state != CSyringe::IDLE)
    return false;

  _state = CSyringe::BUSY;
  _syr_state = DISPENSING;
  _dispense_start = millis();
  _dispense_time = qty;
  
  analogWrite(_suck_pin,0);
  analogWrite(_squirt_pin,150);

  return false;
};

bool CSyringe::loop()
{
  if (_state != CSyringe::BUSY)
    return true;
  
  
  if ((_syr_state == DISPENSING) && (millis()-_dispense_start >= _dispense_time))
  {
    analogWrite(_suck_pin,0);
    analogWrite(_squirt_pin,0);
    _syr_state = SUCK_WAIT;
  }

  else if ((_syr_state == SUCK_WAIT) && (millis()-_dispense_start >= (_dispense_time+SYRINGE_WAIT_TIME)))
  {
    analogWrite(_suck_pin,150);
    analogWrite(_squirt_pin,0);
    _syr_state = SUCKING;
  }

  else if ((_syr_state == SUCKING) && (millis()-_dispense_start >= (_dispense_time+SYRINGE_WAIT_TIME+SYRINGE_SUCK_TIME)))
  {
    analogWrite(_suck_pin,0);
    analogWrite(_squirt_pin,0);
    _syr_state = DRIP_WAIT;
  }

  else if ((_syr_state == DRIP_WAIT) && (millis()-_dispense_start >= (_dispense_time+SYRINGE_WAIT_TIME+SYRINGE_SUCK_TIME+SYRINGE_DRIP_TIME)))
  {
    _state = CSyringe::IDLE;
    _syr_state = READY;
  }

  return true;
}

void CSyringe::stop()
{
  analogWrite(_suck_pin,0);
  analogWrite(_squirt_pin,0);
  _state = CSyringe::IDLE; 
}

CDispenser::dispenser_state CSyringe::get_status()
{
  return _state;
}
