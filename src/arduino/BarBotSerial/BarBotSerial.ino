#include <AccelStepper.h>
#include <Servo.h>

#include <BarBot.h>

#define SERIAL_IN_BUF 50

// Maintenance commands param1.            param2 meaning
#define MAINTENANCE_MODE        0       // 0=leave maint mode, 1=enter maint. mode
#define MAINTENANCE_OPTICS      1       // 0=all optics to idle, 9=all optics dispense
#define MAINTENANCE_MIXERS      2       // 0=all mixers to idle, 9=all mixers dispense
#define MAINTENANCE_DASHER_ON   3       // switch dasher n (0-2) on
#define MAINTENANCE_DASHER_OFF  4       // switch dasher n (0-2) off


void process_message(char *msg);
void process_serial();

BarBot *bb;

void setup()
{
  bb = new BarBot();
  Serial.begin(9600);      // For debug info only
  Serial2.begin(115200);   // Communication with Pi
  Serial.println("Start!");
  
}

void loop()
{
  bb->loop();
  process_serial(); 
}


void process_serial(void) 
{
  static char inputString[SERIAL_IN_BUF];
  static uint8_t bytes_received;
  
 
  while (Serial2.available()) 
  {   
    // get the new byte:
    char inChar = (char)Serial2.read();
    
    // add it to the inputString:
    inputString[bytes_received++] = inChar;
    
    // if the incoming character is a newline, process the buffer as a message
    if ((inChar == '\n') || (inChar == '\r'))
    {
      process_message(inputString);
      memset(inputString, 0, sizeof(inputString));
      bytes_received = 0;
    }
    else if (bytes_received >= (SERIAL_IN_BUF-1))
    {
      Serial.println(F("Message too long. Ignored."));
      memset(inputString, 0, sizeof(inputString));
      bytes_received = 0;
    } 
  }
}

void process_message(char *msg)
{
  char instruction = 0;
  uint16_t param1 = 0;
  uint16_t param2 = 0;
  uint8_t ret;
  BarBot::barbot_state state;
  
  Serial.print("Process: ");
  Serial.println(msg);
  
  ret = sscanf(msg, "%c %d %d", &instruction, &param1, &param2);
  if (ret == 0)
  {
    Serial.println("Invalid mesage");
    return;
  }
  
  state = bb->get_state();
  if ((state == BarBot::RUNNING) && (instruction != 'R'))
  {
    Serial.println("Error - barbot is busy");
    return;
  } 

  switch (instruction)
  {
    case 'C':  // Clear any previously stored insturctions.
      bb->instructions_clear();
      break;
      
    case 'M':  // Move insturction. Param1 should be where to move to.
      if (ret < 1)
      {
        Serial.println(F("Error: parameter missing for Move"));
        return;
      }
      bb->instruction_add(BarBot::MOVE, param1, 0);  
      break;
      
    case 'D': // Dispense insturction. param1 = dispenser_id, param2 misc paramter for dispenser (purpose varies dependant on dispenser type)
      if (ret < 2)
      {
        Serial.println(F("Error: parameter missing for Dispense"));
        return;
      }
      bb->instruction_add(BarBot::DISPENSE, param1, param2);  
      break;
      
    case 'G': // GO!
      bb->go();
      break;
      
    case 'R':  // Reset. Clear all instructions, return to home, set IDLE state
      bb->reset();
      break;
      
    case 'Z':  // Zero
      Serial.println(F("Zero ins"));
      bb->instruction_add(BarBot::ZERO, 0, 0);  
      break;
    
    case 'O': // Order number
      bb->instruction_add(BarBot::DISPLAYNUM, param1, 0);
      break;
      
    case 'A': // mAintenance mode (Because M is already used...)
      // param1 = function, param2 = option
      if (ret < 2)
      {
        Serial.println(F("Error: parameter missing for Maint command"));
        return;
      }
      
      switch (param1)
      {
        case MAINTENANCE_MODE:
          if (param2 == 0)
            bb->maint_mode_leave();
          else if (param2 == 1)
            bb->maint_mode_enter();
          break;
          
        case MAINTENANCE_OPTICS:
          bb->maint_mode_optics(param2);
          break;
          
        case MAINTENANCE_MIXERS:
          bb->maint_mode_mixers(param2);
          break;
          
        case MAINTENANCE_DASHER_ON:
          bb->maint_mode_dasher_on(param2);
          break;
          
        case MAINTENANCE_DASHER_OFF:
          bb->maint_mode_dasher_off(param2);
          break;
          
        default:
          Serial.println("Unexpected maint. param!");
          return;
          break;
      }
     
    default:
      Serial.println("Unexpected instruction!");
      return;
      break;
  }
 
}


