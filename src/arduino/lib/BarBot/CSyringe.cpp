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

  _squirt_done = false;
  _drip_wait = false;
  _state = CSyringe::BUSY;
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

  if (!_drip_wait)
  {
    if ((!_squirt_done) && (millis()-_dispense_start >= _dispense_time))
    {
      _squirt_done = true;
      analogWrite(_suck_pin,0);
      analogWrite(_squirt_pin,0);

      analogWrite(_suck_pin,150);
      analogWrite(_squirt_pin,0);
    } 
    if (_squirt_done && (millis()-_dispense_start >= (_dispense_time+SYRINGE_SUCK_TIME)))
    {
      _last_used = millis();
      analogWrite(_suck_pin,0);
      analogWrite(_squirt_pin,0);
      _drip_wait = true;
    }
  } else
  {
    if (millis()-_dispense_start > (_dispense_time+SYRINGE_SUCK_TIME+SYRINGE_DRIP_TIME))
      _state = CSyringe::IDLE;
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
