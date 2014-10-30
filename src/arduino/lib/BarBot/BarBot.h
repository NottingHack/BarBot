/*
  
*/
#ifndef BARBOT_H
#define BARBOT_H

#include "CDispenser.h"
#include "COptic.h"
#include "CStirrer.h"
#include "CConveyor.h"
#include "CUmbrella.h"
#include "CSlice.h"
#include "CMixer.h"
#include "CDasher.h"
#include "CSyringe.h"
#include "CDisplay.h"

#include "Arduino.h"
#include "AccelStepper.h"
#include <avr/pgmspace.h>
#include "Adafruit_NeoPixel.h"

#define MAX_INSTRUCTIONS    100  // Maximum number of instructions that can be stored

#define MAX_MOVE_TIME      19000  // Maximum amount of time moving the platform should take (in ms).
#define STEPS_PER_CM          48  // Number of steps per CM (platform movement)
#define MAX_RAIL_POSITION   7080  // Maximum number of steps
#define RESET_POSITION     14000  // Position to move to when moving to the limit switch

// Harware setup
#define DISPENSER_COUNT       21  // Number of attached dispensers. If altered, also need to change BarBot::BarBot()
#define ZERO_SWITCH           37  // Zero/limit switch. Nb - zero switch is now the 'other' side, i.e. position ~7000
#define ESTOP_PIN             53  // Emergency stop 
#define SPEED_ZERO           800  // Speed when zeroing
#define SPEED_NORMAL        1500  // Normal speed
#define MAX_ACCEL           3000
#define GLASS_SENSE_PIN       15 
#define PLATFORM_TX           14
#define NEO0_PIN               7 // Dasher neopixel
#define NEO1_PIN               8 // Optic neopixel

#define NEO_DASHER0   0
#define NEO_DASHER1  24
#define NEO_DASHER2  48

// dispenser_id (needs to match db)
#define DISPENSER_OPTIC0     1
#define DISPENSER_OPTIC1     2
#define DISPENSER_OPTIC2     3
#define DISPENSER_OPTIC3     4
#define DISPENSER_OPTIC4     5
#define DISPENSER_OPTIC5     6
#define DISPENSER_PREASURE0  7
#define DISPENSER_PREASURE1  8
#define DISPENSER_PREASURE2  9
#define DISPENSER_PREASURE3 10
#define DISPENSER_PREASURE4 11
#define DISPENSER_PREASURE5 12
#define DISPENSER_DASHER0   13
#define DISPENSER_DASHER1   14
#define DISPENSER_DASHER2   15
#define DISPENSER_CONVEYOR  16
#define DISPENSER_SYRINGE   17
#define DISPENSER_SLICE     18
#define DISPENSER_STIRRER   19
#define DISPENSER_UMBRELLA  20

// Dummy optic values for controlling the neopixels above them
#define DISPENSER_OPTIC_NONE  -1  // Not currently dispencing from an optic
#define DISPENSER_OPTIC_FAULT -2  // Turn all neopixels red to indicate something bad has heppened.

void debug(char *msg);


class BarBot
{
  public:
    enum instruction_type
    {
      NOP,
      MOVE,       // Move to position <param1>
      DISPENSE,   // Dispense using dispenser <param1> with <param2>
      WAIT,       // Wait for <param1> ms
      ZERO,       // Move platform until it hits the limit switch, then call that 0
      DISPLAYNUM  // Show <param1> on external display
    };

    enum barbot_state
    {
      IDLE,
      WAITING,
      RUNNING,
      FAULT
    };
    
    BarBot();
    ~BarBot();
    bool instruction_add(instruction_type instruction, uint16_t param1, uint16_t param2);
    bool instructions_clear();
    bool go();
    bool reset();
    bool loop();
    barbot_state get_state();

      
  private:
    struct instruction
    {
      instruction_type  type;
      uint16_t          param1;
      uint16_t          param2;
    };
         
    bool exec_instruction(uint16_t instruction);
    void move_to(long pos);
    void move_to(long pos, bool force);
    void set_state(barbot_state state);
    void color_wipe(uint32_t c, uint8_t dasher);
    void color_wipe(uint32_t c);
    void refresh_neo();
    void dasher_wheel(uint8_t dasher);
    void set_neo_colour(barbot_state state);
    void optic_neo(int active_optic);
    
    barbot_state _state;
    instruction _instructions[MAX_INSTRUCTIONS];
    uint16_t _instruction_count;
    uint16_t _current_instruction;
    unsigned long long _wait_inst_start;
    unsigned long long _move_start;
    AccelStepper *_stepper;
    CDisplay *_display;
    long _stepper_target;
    CDispenser *_dispeners[DISPENSER_COUNT];
    bool glass_present();
    Adafruit_NeoPixel *_dasher_neo;
    Adafruit_NeoPixel *_optic_neo;
    uint32_t _neo_buf[96];
};

#endif
