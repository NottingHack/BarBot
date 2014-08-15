
// Nottinghack Barbot Platform
// Ultrasonic glass detection and Neopixel illumination
// ATtiny85 (Needs 8 MHz clock for Neopixel library)
// V0.4 - Diagnostics mode check at at start up removed, amber / green cycles reduced, glass removal check now more responsive

// NeoPixel ring
#include <Adafruit_NeoPixel.h>
#define NEO_PIN 1

// Ultrasonic distance sensor 
#define TRIG_PIN  3
#define ECHO_PIN  4

// Glass distance range in cm 
#define MIN_GLASS_CM 1
#define MAX_GLASS_CM 18

// Glass present output
#define GP_PIN 0

// Serial RX pin
#define RX_PIN 2

typedef enum {RED,AMBER,GREEN,READY} 
glassState_t;
glassState_t glassState=RED;

int LED_cycles;
#define NUM_CYCLES 2

Adafruit_NeoPixel strip = Adafruit_NeoPixel(24,NEO_PIN,NEO_GRB+NEO_KHZ800);

void setup() 
{
  pinMode(TRIG_PIN,OUTPUT);
  pinMode(ECHO_PIN,INPUT);
  pinMode(GP_PIN,OUTPUT);
  pinMode(RX_PIN,INPUT_PULLUP);

  digitalWrite(GP_PIN,LOW);

  strip.begin();
  strip.show();
}

void loop() 
{
  int d=readDistance();
  boolean glassPresent=(d>=MIN_GLASS_CM && d<=MAX_GLASS_CM);

  switch(glassState)
  {
  case RED:
    if(glassPresent)
    {
      glassState=AMBER;
      LED_cycles=NUM_CYCLES;
    } 
    else
    {
      colorWipe(strip.Color(255,0,0),10); // red
      colorWipe(strip.Color(0,0,0),10);   // blank
    }
    break;

  case AMBER:
    if(glassPresent)
    {
      if(LED_cycles<=0)
      {
        glassState=GREEN;
        LED_cycles=NUM_CYCLES;
      }
      else
      {
        colorWipe(strip.Color(255,80,0),10); // amber
        colorWipe(strip.Color(0,0,0),10);    // blank
        LED_cycles--;
      }
    }
    else
    {
      digitalWrite(GP_PIN,LOW);
      glassState=RED;        
    }
    break;

  case GREEN:
    if(glassPresent)
    {
      if(LED_cycles<=0)
      {
        glassState=READY;
        digitalWrite(GP_PIN,HIGH);
      }
      else
      {
        colorWipe(strip.Color(0,255,0),10); // green
        colorWipe(strip.Color(0,0,0),10);   // blank
        LED_cycles--;
      }
    }
    else
    {
      digitalWrite(GP_PIN,LOW);
      glassState=RED;        
    }
    break;

  case READY:
    if(glassPresent)
    {
      rainbowCycle(10);
    }
    else
    {
      digitalWrite(GP_PIN,LOW);
      glassState=RED;        
    }
    break;

  default:
    digitalWrite(GP_PIN,LOW);
    glassState=RED;
  }
}

void showDistance(int d)
{
  for(int i=0;i<strip.numPixels();i++)
  {
    if(i==d) strip.setPixelColor(i,strip.Color(0,0,255));
    else strip.setPixelColor(i,strip.Color(0,0,0));
  }

  strip.show();
}

void colorWipe(uint32_t c, uint8_t wait) 
{
  for(uint16_t i=0; i<strip.numPixels(); i++) 
  {
    strip.setPixelColor(i, c);
    strip.show();
    delay(wait);
  }
}

void rainbowCycle(uint8_t wait) 
{
  static uint16_t j;
  uint16_t i;
  
  j++;
  if(j>255) j=0;

  for(i=0; i< strip.numPixels(); i++) 
  {
      strip.setPixelColor(i, Wheel(((i * 256 / strip.numPixels()) + j) & 255));
  }
  strip.show();
  delay(wait);
}

uint32_t Wheel(byte WheelPos) 
{
  if(WheelPos < 85) 
  {
    return strip.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
  } 
  else if(WheelPos < 170) 
  {
    WheelPos -= 85;
    return strip.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  } 
  else 
  {
    WheelPos -= 170;
    return strip.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
}

int readDistance()
{
  long d;

  digitalWrite(TRIG_PIN,LOW); 
  delayMicroseconds(2); 

  digitalWrite(TRIG_PIN,HIGH);
  delayMicroseconds(10); 

  digitalWrite(TRIG_PIN,LOW);

  //Calculate the distance (in cm) based on the speed of sound.
  d=pulseIn(ECHO_PIN,HIGH,10000)/58.2;

  return d;
} 






