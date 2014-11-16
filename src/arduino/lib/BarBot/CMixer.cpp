#include "CMixer.h"

/* mixer dispenser - qty is ml to dispense */

CMixer::CMixer(uint8_t servo_pin)
{
  _servo_pin = servo_pin;
  _servo.write(MIXER_IDLE_POSITION);
  _last_used = millis();
  _state = CMixer::IDLE;
  _attached = false;
}

CMixer::~CMixer()
{
  if (_attached)
    _servo.detach();
}

uint8_t CMixer::get_dispener_type()
{
  return DISPENSER_MIXER;
}

// qty = milliseconds to dispense for
bool CMixer::dispense(uint16_t qty)
{
  if (_state != CMixer::IDLE)
    return false;

  if (!_attached)
  {
    _servo.attach(_servo_pin);
    _attached = true;
  }
  
  _state = CMixer::BUSY;
  _dispense_start = millis();
  _servo.write(MIXER_DISPENSE_POSITION);
  _dispense_time = qty ;
  _dispensed = false;

  return false;
};

bool CMixer::loop()
{
  if (_attached && (_state != CMixer::BUSY) && (millis()-_last_used > MIXER_DETTACH_TIME))
  {
    _servo.detach();
    _attached = false;
  }

  if (_state != CMixer::BUSY)
    return true;

  if ((!_dispensed) && (millis()-_dispense_start >= _dispense_time))
  {
    _servo.write(MIXER_IDLE_POSITION);
    _dispensed = true;
  } 
  
  if (_dispensed && (millis()-_dispense_start >= (_dispense_time + MIXER_DELAY)))
  {
    _last_used = millis();
    _state = CMixer::IDLE;
  }

  return true;
}

void CMixer::stop()
{
  if (_attached)
  {
    _servo.write(MIXER_IDLE_POSITION);
    _last_used = millis();
  }
  _state = CMixer::IDLE; 
}

// Move to idle postion. Important: Should only ever be called when in maintenance mode.
void CMixer::move_to_idle()
{
  if (!_attached)
  {
    _servo.attach(_servo_pin);
    _attached = true;
  }  
  _servo.write(MIXER_IDLE_POSITION);  
}

// Move to dispense postion. Important: Should only ever be called when in maintenance mode.
void CMixer::move_to_dispense()
{
  if (!_attached)
  {
    _servo.attach(_servo_pin);
    _attached = true;
  }  
  _servo.write(MIXER_DISPENSE_POSITION);  
}

CDispenser::dispenser_state CMixer::get_status()
{
  return _state;
}
