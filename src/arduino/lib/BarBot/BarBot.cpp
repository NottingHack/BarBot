#include "BarBot.h"

BarBot::BarBot()
{
  wdt_disable();
  Serial3.begin(9600);
  // Dasher neopixel rings
  _dasher_neo = new Adafruit_NeoPixel(72,NEO0_PIN,NEO_GRB+NEO_KHZ800);
  _optic_neo = new Adafruit_NeoPixel(6,NEO1_PIN,NEO_GRB+NEO_KHZ800);
  _dasher_neo->begin();
  _optic_neo->begin();
  _optic_neo->show(); // Initialize all pixels to 'off'
  _dasher_neo->show();
  memset(_neo_buf, 0, sizeof(_neo_buf));
  color_wipe(_dasher_neo->Color(100,100,100)); // white
  refresh_neo();
  optic_neo(DISPENSER_OPTIC_NONE); // None dispening
  
  memset(_instructions, NOP, sizeof(_instructions));
  _instruction_count = 0;
  
  // Note: deliberately skipping ix=0 so array index matches dispenser.id in the database 
  for (int ix=1; ix < DISPENSER_COUNT; ix++)
  {
    switch(ix)
    {
      case DISPENSER_OPTIC0:  _dispeners[ix]   = new COptic(40, 65, 10); break; // Optic0
      case DISPENSER_OPTIC1:  _dispeners[ix]   = new COptic(42, 10, 65); break; // Optic1
      case DISPENSER_OPTIC2:  _dispeners[ix]   = new COptic(44, 65, 10); break; // Optic2
      case DISPENSER_OPTIC3:  _dispeners[ix]   = new COptic(46, 10, 65); break; // Optic3
      case DISPENSER_OPTIC4:  _dispeners[ix]   = new COptic(48, 65, 10); break; // Optic4
      case DISPENSER_OPTIC5:  _dispeners[ix]   = new COptic(50, 65, 10); break; // Optic5

      case DISPENSER_PREASURE0: _dispeners[ix] = new CMixer(41); break; // Preasure0
      case DISPENSER_PREASURE1: _dispeners[ix] = new CMixer(43); break; // Preasure1
      case DISPENSER_PREASURE2: _dispeners[ix] = new CMixer(45); break; // Preasure2 
      case DISPENSER_PREASURE3: _dispeners[ix] = new CMixer(47); break; // Preasure3
      case DISPENSER_PREASURE4: _dispeners[ix] = new CMixer(49); break; // Preasure4
      case DISPENSER_PREASURE5: _dispeners[ix] = new CMixer(51); break; // Preasure5
        
      case DISPENSER_DASHER0:  _dispeners[ix]  = new CDasher(22, 23);  break; // Dasher0
      case DISPENSER_DASHER1:  _dispeners[ix]  = new CDasher(24, 25);  break; // Dasher1
      case DISPENSER_DASHER2:  _dispeners[ix]  = new CDasher(26, 27);  break; // Dasher2
        
      case DISPENSER_SYRINGE:  _dispeners[ix]  = new CSyringe(5,6);     break;  // Syringe
        
      case DISPENSER_CONVEYOR: _dispeners[ix]  = new CConveyor(38, 39); break;  // Conveyor
        
      case DISPENSER_SLICE:    _dispeners[ix]  = new CSlice(34);        break;  // Slice dispenser
        
      case DISPENSER_STIRRER:  _dispeners[ix]  = new CStirrer(36);      break;  // Stirrer
        
      case DISPENSER_UMBRELLA: _dispeners[ix]  = new CUmbrella(32);     break;  // Umbrella
    }
  }
  
  // Stepper for platform movement
  _stepper = new AccelStepper(AccelStepper::DRIVER);
  _stepper->setMaxSpeed(SPEED_NORMAL);
  _stepper->setAcceleration(MAX_ACCEL);
  _stepper->setPinsInverted(false,false,false,false,true);
  _stepper->setEnablePin(4);
  _stepper->disableOutputs();
  if (digitalRead(ZERO_SWITCH) == LOW)
  {
    _stepper_target = MAX_RAIL_POSITION;
    _stepper->setCurrentPosition(MAX_RAIL_POSITION);
  }
  else
  {
    _stepper->setCurrentPosition(0);
    _stepper_target = 0;
  }
  _stepper->run();
  
  // Display for order number output
  _display = new CDisplay(28, 29);
  _current_instruction = 0;
  
  pinMode(ZERO_SWITCH    , INPUT_PULLUP);
  pinMode(ESTOP_PIN      , INPUT_PULLUP);
  pinMode(GLASS_SENSE_PIN, INPUT_PULLUP);
  
  if (digitalRead(ESTOP_PIN) == LOW)
    set_state(BarBot::IDLE);  // estop not pressed
  else
    set_state(BarBot::FAULT); // estop pressed

  //pinMode(PLATFORM_TX, OUTPUT);
  //digitalWrite(PLATFORM_TX, LOW);
  Serial3.print('0'); // Switch platform neopixel to Auto    
}

BarBot::~BarBot()
{
  for (int ix=1; ix < DISPENSER_COUNT; ix++)
    if (_dispeners[ix] != NULL)
      delete _dispeners[ix];
}

// Add an instruction to be carried out
// Returns: true if instruction added, false otherwise
bool BarBot::instruction_add(instruction_type instruction, uint16_t param1, uint16_t param2)
{
  if (_state == BarBot::RUNNING)
  {
    debug("Error: barbot running, can't add");
    return false;
  } 

  // For DISPENSE instructions, param1 is the dispenser_id - ensure this is valid.
  if ((instruction == BarBot::DISPENSE) && (param1 >= DISPENSER_COUNT))
    return false;    
  
  if (_instruction_count < MAX_INSTRUCTIONS)
  {
    _instructions[_instruction_count].type   = instruction;
    _instructions[_instruction_count].param1 = param1;
    _instructions[_instruction_count].param2 = param2;
    _instruction_count++;
    return true;
  }
  else
  {
    return false;
  }
}

// Clear the insturction list
bool BarBot::instructions_clear()
{
  if (_state != BarBot::RUNNING)
  {
    memset(_instructions, NOP, sizeof(_instructions));  
    _instruction_count = 0;
    return true;
  }
}

bool BarBot::reset()
{
  set_state(BarBot::FAULT); // Stop everything
  instructions_clear();
  set_state(BarBot::IDLE);
}

// Make the drink!
// Returns true if drink making has started, false otherwise
bool BarBot::go()
{  
  if (_state != BarBot::IDLE)
  {
    debug("go failed: not idle");
    return false;
  }
  
  if (_instruction_count <= 0)
  {
    debug("go failed: no instructions");
    return false;
  }
  
  _current_instruction = 0;
  
  if (!glass_present())
  {
    debug("Wait for glass");
    set_state(BarBot::WAITING); // Waiting for glass
  } else
  {
    exec_instruction(_current_instruction);
    
    set_state(BarBot::RUNNING);
  }

  return true;
}


bool BarBot::exec_instruction(uint16_t ins)
{
  instruction *cmd = &_instructions[ins];
  char buf[40]="";

  if (ins >= _instruction_count)
    return false;

  sprintf(buf, "exec ins[%d], typ[%d], p1[%d] p2[%d]", ins, cmd->type, cmd->param1, cmd->param2);
  debug(buf);

  switch (cmd->type)
  {
    case NOP:
      break;

    case MOVE:
      _stepper->setMaxSpeed(SPEED_NORMAL);
      move_to(cmd->param1);
      _stepper->run();
      Serial3.print('0'); // Switch platform neopixel to Auto
      break;

    case DISPENSE:
      if (_dispeners[cmd->param1] != NULL)
      {
        _dispeners[cmd->param1]->dispense(cmd->param2); // nb. instruction_add validated that param1 was in bounds.
        Serial3.print('W'); // Platform neopixel to White whilst dispening
       }
     break;

    case WAIT:
      _wait_inst_start = millis();
      break;

    case ZERO:
      _stepper->setMaxSpeed(SPEED_ZERO);
      move_to(RESET_POSITION, true);
      _stepper->run();
      break;

    case DISPLAYNUM:
      _display->setOutput(cmd->param1);
      break;
  }

  // If we're not dispening from an optic, make sure no particular optic is highlighted
  if 
  (
    (cmd->type != DISPENSE) ||
    ((cmd->type == DISPENSE) && (cmd->param2 <= DISPENSER_OPTIC5))
  )
  {
    optic_neo(DISPENSER_OPTIC_NONE);
  }

  return true;
}

// Needs to be called regulary whilst barbot is in action!
bool BarBot::loop()
{
  instruction *cmd = &_instructions[_current_instruction];
  bool done = false;
  bool limit_switch_hit = false;


  if (_state != BarBot::MAINT) // platform is disabled in maintenance mode.
  {
    _stepper->run();
    glass_present();
  }
    
  for (int ix=1; ix < DISPENSER_COUNT; ix++)
    if (_dispeners[ix] != NULL)
    {
      _dispeners[ix]->loop();
    }

  // If in maintenance mode, there's nothing else to do.
  if (_state == BarBot::MAINT)
    return false;

  _stepper->run();
  
  // If the limit switch is hit, stop the platform, unless
  // only just started moving away from the limit switch
    if 
  (
    (digitalRead(ZERO_SWITCH) == LOW) &&
    (
      (_stepper->targetPosition() > _stepper->currentPosition()) || // trying to move towards/beyond the limit switch, or
      (millis()-_move_start > 250)                                  // move has been in progress long enough to have moved off limit switch
    )
  )
  {
    limit_switch_hit = true;
    
    // If we're either zeroing (target=RESET_POSITION), or moving to the end of the rail, reset the home position
    if ((_stepper_target == RESET_POSITION) || ((_stepper_target==MAX_RAIL_POSITION) && (_stepper->distanceToGo() < 100)))
    {
      _stepper->setCurrentPosition(MAX_RAIL_POSITION);
      _stepper->stop();
      _stepper->disableOutputs();
    } else
    {
      _stepper->stop();
      _stepper->disableOutputs();
      if (_state != BarBot::FAULT)
      {
        debug("Error: limit switch unexpectedly hit!");
        set_state(BarBot::FAULT);
      }
    }
  }
  
  _stepper->run();
  
  // Look for Emergency stop button being pressed
  if ((_state != BarBot::FAULT) && (digitalRead(ESTOP_PIN) == HIGH))
  {
    debug("ESTOP");
    set_state(BarBot::FAULT);
  }
  
  // If waiting (for a glass), and a glass is now present, start making the drink
  if (_state == BarBot::WAITING)
  {
    if (glass_present())
    {
      exec_instruction(_current_instruction);
        
      set_state(BarBot::RUNNING);
      return false;
    }
  }
  
  // If in the process of making a drink, and the glass has been removed, stop
  if ((_state == BarBot::RUNNING) && (!glass_present()))
  {
    debug("Glass removed.");
    set_state(BarBot::FAULT);
    return false;
  }

  // If running, find out if the last executed insturction has finished 
  if (_state == BarBot::RUNNING)
  {
    switch (cmd->type)
    {
      case NOP:
        done = true;
        break;
        
      case MOVE:
        if (_stepper->distanceToGo() == 0)
        {
          //_stepper->disableOutputs();
          done = true;
        }
        if ((millis()-_move_start) > MAX_MOVE_TIME)
        {
          debug("Move timeout!");
          set_state(BarBot::FAULT);
        } 
        break;
        
      case DISPENSE:
        if (_dispeners[cmd->param1] != NULL)
        {
          
          
          // If dispening from an optic, change the colour of the neopixel above the optic
          if (_dispeners[cmd->param1]->get_dispener_type() == CDispenser::DISPENSER_OPTIC)
          {
            optic_neo((cmd->param1) - 1); // Neopixels are 0-6 for optic/dispenser id's 1-7
          }
            
          // If dispening from a dasher, animate the neopixel ring
          if (cmd->param1 == DISPENSER_DASHER0)
            dasher_wheel(NEO_DASHER0);
          else if (cmd->param1 == DISPENSER_DASHER1)
            dasher_wheel(NEO_DASHER1);
          else if (cmd->param1 == DISPENSER_DASHER2)
            dasher_wheel(NEO_DASHER2);
          if (_dispeners[cmd->param1]->get_status() == CDispenser::IDLE)
          {
            if (_dispeners[cmd->param1]->get_dispener_type() == CDispenser::DISPENSER_DASHER)
              set_neo_colour(_state); // restore previous colour
            done = true;
          }
        } else
          done = true;
        break;
        
      case WAIT:
        if ((millis()-_wait_inst_start) >= cmd->param1)
          done = true;
        break;
        
      case ZERO:
        if (digitalRead(ZERO_SWITCH) == LOW)
        {
          _stepper->stop();
          _stepper->disableOutputs();
          _stepper->setCurrentPosition(MAX_RAIL_POSITION);
          done = true;
        } 
        else if (_stepper->distanceToGo() == 0)
        {
          debug("FAULT: distanceToGo=0 whilst zeroing!");
          set_state(BarBot::FAULT);
        }
        else if (millis()-_move_start > MAX_MOVE_TIME)
        {
          debug("FAULT: ZERO timeout");
          set_state(BarBot::FAULT);
        }
        break;

	  case DISPLAYNUM :
        done = !_display->isBusy();
        break;
    }
  
    _stepper->run();
  
    if (done)
    {
      if (!exec_instruction(++_current_instruction))
      {
        // exec_instruction returns false when there are no more instructions to execute.
        debug("Done! setting state=idle");
        _stepper->disableOutputs();
        set_state(BarBot::IDLE);
      }
    }
  }
  
  return false;
}

void BarBot::set_state(barbot_state new_state)
{  
  static bool first_run = true;
  
  Serial3.print('0');
  
  // Because maintenance mode does weird things, the only way out at the moment
  // is a (potentially soft) reset.
  if (new_state == BarBot::MAINT)
  {
    debug("ERROR: In maint mode");
    return;
  }
  
  if (new_state == BarBot::FAULT)
  {
    debug("FAULT.");

    // Stop platform
    _stepper->stop();

    _stepper->run();
    
    // Stop all dispensers
    for (int ix=1; ix < DISPENSER_COUNT; ix++)
      if (_dispeners[ix] != NULL)
        _dispeners[ix]->stop();
    _stepper->disableOutputs();
  }
  
  // Don't allow leaving FAULT state if emergency stop pressed
  if 
  (
    (_state == BarBot::FAULT)    && 
    (new_state != BarBot::FAULT) &&
    (digitalRead(ESTOP_PIN) == HIGH)
  )
  {
    debug("ESTOP ACTIVE");
    return;
  }   
  
  if ((_state != new_state) || first_run)
  {
  _state = new_state;  
    first_run = false;
    
    set_neo_colour(new_state);
  }
}

void BarBot::move_to(long pos)
{
  move_to(pos, false);
}

void BarBot::move_to(long pos, bool force)
{
  // Sanity check - if emergency stop pressed, don't start moving
  if (digitalRead(ESTOP_PIN) == HIGH)
  {
    _stepper->stop();
    _stepper->disableOutputs();
    debug("move: fail - ESTOP");
    return;
  }
  char buf[30]="";
  if (!force && (pos > MAX_RAIL_POSITION))
  {
    pos = MAX_RAIL_POSITION;
    debug("Excessive rail position");
  }
  _stepper->enableOutputs();
  _stepper_target = pos;
  _stepper->moveTo(pos);
  _move_start = millis();
  sprintf(buf, "move=%d", pos);
  debug(buf);
}

BarBot::barbot_state BarBot::get_state()
{
  return _state;
}
   
bool BarBot::glass_present()
{
  static uint8_t delay;
  static int last_state;
  static int glass_state;
  static unsigned long long last_state_change;
  int current_state;
  
  if (last_state_change==0)
    glass_state = digitalRead(GLASS_SENSE_PIN);
  
  if (delay++ != 0)
    return glass_state;

  current_state = digitalRead(GLASS_SENSE_PIN);

  if (current_state != last_state)
    last_state_change = millis();

  if ((millis() - last_state_change) > 100) 
  {
    if (current_state != glass_state)
      glass_state = current_state;
  }
  last_state = current_state;
  return (glass_state==HIGH ? true : false);
}

void BarBot::color_wipe(uint32_t c)
  {
  color_wipe(c, NEO_DASHER0);
  color_wipe(c, NEO_DASHER1);
  color_wipe(c, NEO_DASHER2);
  }
void BarBot::color_wipe(uint32_t c, uint8_t dasher)
{
  for(uint16_t i=dasher; (i<_dasher_neo->numPixels()) && (i < dasher+24); i++) 
    _neo_buf[i] = c;
}
   
void BarBot::dasher_wheel(uint8_t dasher)
{
  static uint8_t pos = 0;
  static unsigned long last_update;
  
  if (dasher+24 > sizeof(_neo_buf))
    return;
  
  if (millis()-last_update > 10)
  {
    _neo_buf[pos+dasher] = _dasher_neo->Color(0,100,0);     // Green
    pos = (pos+1)%24;
    _neo_buf[pos+dasher] = _dasher_neo->Color(255,255,255); // White
    refresh_neo();
    last_update = millis();
  }
}

void BarBot::set_neo_colour(barbot_state state)
{
  // Change dasher new pixel rings colour
  switch (state)
  {
    case BarBot::IDLE:
      color_wipe(_dasher_neo->Color(100,100,100)); // white
      optic_neo(DISPENSER_OPTIC_NONE); // None dispening
      break;

    case BarBot::WAITING:
      color_wipe(_dasher_neo->Color(100,100,0));  // yellow
      optic_neo(DISPENSER_OPTIC_NONE); // None dispening
      break;

    case BarBot::RUNNING:
      color_wipe(_dasher_neo->Color(0,100,0));    // green
      optic_neo(DISPENSER_OPTIC_NONE); // None dispening
      break;
      
    case BarBot::MAINT:
      color_wipe(_dasher_neo->Color(255,0,255));    // purple
      optic_neo(DISPENSER_OPTIC_NONE); // None dispening
      break;      

    case BarBot::FAULT:
      color_wipe(_dasher_neo->Color(100,0,0));    // red
      optic_neo(DISPENSER_OPTIC_FAULT); 
      break;
  }
  refresh_neo();
}

/* Highlight <active_optic> optic in a different colour */
/* Nb. DISPENSER_OPTIC_NONE won't match any neopixel, so results in all being set to white */
void BarBot::optic_neo(int active_optic)
{
  for(uint16_t i=0; i <_optic_neo->numPixels(); i++)
  {
    if (active_optic == DISPENSER_OPTIC_FAULT)
    {
      _optic_neo->setPixelColor(i, _optic_neo->Color(255,0,0)); // red
    } else
    {
      if (i == active_optic)
      {
        _optic_neo->setPixelColor(i, _optic_neo->Color(255,255,255));
      } else
      {
        _optic_neo->setPixelColor(i, _optic_neo->Color(0,0,255));
      }
    }
  }
  _optic_neo->show();
}

void BarBot::refresh_neo()
{
  for(uint16_t i=0; ((i <_dasher_neo->numPixels()) && (i < sizeof(_neo_buf))); i++)
    _dasher_neo->setPixelColor(i, _neo_buf[i]);
  _dasher_neo->show();
}

void debug(char *msg)
{
  Serial.println(msg);
  Serial2.print("I ");
  Serial2.println(msg);
}

// Enter maintenance mode
bool BarBot::maint_mode_enter()
{
  if (_state != BarBot::IDLE)
  {
    debug("Not idle!");
    return false;
  }
  
  set_state(BarBot::FAULT); // Going though the FAULT state should ensure all dispensers are stopped
  set_state(BarBot::MAINT);
  
  Serial3.print('Z'); // Switch off platform neopixels
  _stepper->stop();
  _stepper->disableOutputs();
  return (_state == BarBot::MAINT);
}

bool BarBot::maint_mode_leave()
{
  // The easiest way to get back into a known state is just to reset
  wdt_enable(WDTO_2S); // Watchdog abuse...
  while(1);
  return false;
}

// Optic maintenance
// param=0 - all optics to idle. param=9 - all optics to dispense position.
void BarBot::maint_mode_optics(uint8_t param)
{
  switch (param)
  {
    case 0:
      for (int ix=1; ix < DISPENSER_COUNT; ix++)
      {
        if (_dispeners[ix]->get_dispener_type() == CDispenser::DISPENSER_OPTIC)       
          ((COptic*)_dispeners[ix])->move_to_idle();
      }
      break;
    
    case 9:
      for (int ix=1; ix < DISPENSER_COUNT; ix++)
      {
        if (_dispeners[ix]->get_dispener_type() == CDispenser::DISPENSER_OPTIC)       
          ((COptic*)_dispeners[ix])->move_to_dispense();
      }
      break;
      
    default:
      break;
  }
  
}

// Mixer maintenance
// param=0 - all mixers to idle. param=9 - all mixers to dispense position.
void BarBot::maint_mode_mixers(uint8_t param)
{
  switch (param)
  {
    case 0:
      for (int ix=1; ix < DISPENSER_COUNT; ix++)
      {
        if (_dispeners[ix]->get_dispener_type() == CDispenser::DISPENSER_MIXER)       
          ((CMixer*)_dispeners[ix])->move_to_idle();
      }
      break;
    
    case 9:
      for (int ix=1; ix < DISPENSER_COUNT; ix++)
      {
        if (_dispeners[ix]->get_dispener_type() == CDispenser::DISPENSER_MIXER)       
          ((CMixer*)_dispeners[ix])->move_to_dispense();
      }
      break;
      
    default:
      break;
  }
}

void BarBot::maint_mode_dasher_on(uint8_t dasher)
{
  // There are three dashers, so dasher must be 0-2
  if (dasher > 2)
    return;
  
  ((CDasher*)_dispeners[DISPENSER_DASHER0+dasher])->maint_dasher_on();
}

void BarBot::maint_mode_dasher_off(uint8_t dasher)
{
  // There are three dashers, so dasher must be 0-2
  if (dasher > 2)
    return;
  
  ((CDasher*)_dispeners[DISPENSER_DASHER0+dasher])->maint_dasher_off();
}

