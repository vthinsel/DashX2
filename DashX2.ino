/*
  This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
  http://creativecommons.org/licenses/by-nc-sa/4.0/legalcode
  Check README.md for further details and usage
*/
#include "LedControl.h"
#include <EEPROMex.h>
#include <SoftwareSerial.h>
#include "DashX2.h"
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif
#undef _EEPROMEX_DEBUG //to prevent false alarms on EEPROM write execeeded

// ===============================
// = Change to match your config =
// ===============================
#define DELAY 0
//#define DEBUG
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      24 // Number of RGB LEDS in ribbon
// On which PIN are the NeoPixel attached to ?
#define PIN            6
// NeoPixel right/left or left/right orientation
#define REVERSERPMLEDS
// MAX7219 pinout (Data,Clock,Strobe,number of cascaded modules)
LedControl lc = LedControl(10, 11, 12, 5);
// MAX7219 cascaded modules order. Depends on the soldering
#define SEGMODULE1 0
#define SEGMODULE2 2
#define SEGMODULE3 4
#define SEGMODULE4 3
#define GEARMODULE 1

// ======================
// = DO NOT TOUCH BELOW =
// ======================
unsigned int valueA;
unsigned int valueB;
unsigned int valueC;
unsigned int valueD;
unsigned int valueE;
unsigned int valueF;
unsigned int valueH;
unsigned int valueI;
unsigned int carrpm;           // holds the rpm data (0-65535 size)
unsigned int cargear;          // holds gear value data (0-65535 size)
unsigned int rpmmax = 1000;    // autocalibrating initial RPM max val

byte rpmredleds; // number of leds that will be red
byte rpmorangeleds; // number of leds that will be orange
byte rpmpercent; // Range to light leds
byte speedmult;
byte intensity;
bool reverse;

// RPM related stuff
unsigned int rpmmin;
int rpmrange;
unsigned int ledweight;
int rpmlearn;
// the possible states of the state-machine
typedef enum { NONE, GOT_RPMLEARN, GOT_INTENSITY, GOT_SPEEDMULT, GOT_A, GOT_B, GOT_C, GOT_D, GOT_E, GOT_F, GOT_H, GOT_I, GOT_R, GOT_G, GOT_STOP, GOT_START, GOT_MAXRPM , GOT_RPMPCT, GOT_RPMREDLEDS, GOT_RPMORANGELEDS} states;

// current state-machine state
states state = NONE;
// current partial number
signed int currentValue;

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);
#define RPMRESET 5

void processMaxRPM(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("MaxRPM = ")); Serial.println(value);
#endif
  rpmmax = value;
  CalcRPMRange();
}

void processRPM(const unsigned int value)
{
  // do something with RPM
#if defined DEBUG
  Serial.print(F("RPM = ")); Serial.println(value);
#endif
  carrpm = value;
  if (carrpm > rpmmax && carrpm < 13000 && rpmlearn == 1) {
    rpmmax = carrpm;
    CalcRPMRange();
  }
}


void processGear(const unsigned int value)
{
  // do something with gear
#if defined DEBUG
  Serial.print(F("Gear = ")); Serial.println(value);
#endif
  cargear = value;
  printGear(cargear);
}

void printNumber(int module, int offset, int v) {
  int ones;
  int tens;
  int hundreds;
  int thousands;

  if ( v > 9999) {
    v = v / 10;
  }
  if ( v > 9999) {
    v = 9999;
  }
  ones = v % 10;
  v = v / 10;
  tens = v % 10;
  v = v / 10;
  hundreds = v % 10 ;
  v = v / 10;
  thousands = v;
  //Now clean digits not used
  if (thousands == 0) {
    lc.setChar(module, 3 + offset, ' ', false);
    if (hundreds == 0) {
      lc.setChar(module, 2 + offset, ' ', false);
      if (tens == 0) {
        lc.setChar(module, 1 + offset, ' ', false);
        if (ones == 0) {
          lc.setDigit(module, 0 + offset, (byte)0, true);
        }
      }
    }
  }
  //Now print the number digit by digit
  if (thousands) {
    lc.setDigit(module, 3 + offset, (byte)thousands, false);
    lc.setDigit(module, 2 + offset, (byte)hundreds, false);
    lc.setDigit(module, 1 + offset, (byte)tens, false);
    lc.setDigit(module, 0 + offset, (byte)ones, true);
  }
  else if (hundreds) {
    lc.setDigit(module, 2 + offset, (byte)hundreds, false);
    lc.setDigit(module, 1 + offset, (byte)tens, false);
    lc.setDigit(module, 0 + offset, (byte)ones, true);
  }
  else if (tens) {
    lc.setDigit(module, 1 + offset, (byte)tens, false);
    lc.setDigit(module, 0 + offset, (byte)ones, true);
  }
  else if (ones) {
    lc.setDigit(module, 0 + offset, (byte)ones, true);
  }
}
void processA(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueA = ")); Serial.println(value);
#endif
  valueA = value;
  printNumber(SEGMODULE1, 4 , value);
}

void processB(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueB = ")); Serial.println(value);
#endif
  valueB = value;
  printNumber(SEGMODULE1, 0, value);
}

void processC(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueC = ")); Serial.println(value);
#endif
  valueC = value;
  printNumber(SEGMODULE2, 4, value);
}

void processD(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueD = ")); Serial.println(value);
#endif
  valueD = value;
  printNumber(SEGMODULE2, 0, value);
}

void processE(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueE = ")); Serial.println(value);
#endif
  valueE = value;
  printNumber(SEGMODULE3, 4, value);
}

void processF(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueF = ")); Serial.println(value);
#endif
  valueF = value;
  printNumber(SEGMODULE3, 0, value);
}

void processH(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueH = ")); Serial.println(value);
#endif
  valueH = value;
  printNumber(SEGMODULE4, 4, value);
}

void processI(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("ValueI = ")); Serial.println(value);
#endif
  valueI = value;
  printNumber(SEGMODULE4, 0, value);
}

void processStop(const unsigned int value)
{ // We just clear everything
#if defined DEBUG
  Serial.print(F("Stop = ")); Serial.println(value);
#endif
  int devices = lc.getDeviceCount();
  for (int address = 0; address < devices; address++) {
    lc.clearDisplay(address);
  }
  pixels.clear();
  pixels.show();
}

void processStart(const unsigned int value)
{
#if defined DEBUG
  Serial.print(F("Start = ")); Serial.println(value);
#endif
  rpmmax = 1000;
}

void printGear(unsigned int gear)
{
  if (reverse == true) {
    gear = 'r';
  }
  switch (gear) {
      /*
        case 0:
        lc.setRow(GEARMODULE, 0, B00111000);
        lc.setRow(GEARMODULE, 1, B01000100);
        lc.setRow(GEARMODULE, 2, B01000100);
        lc.setRow(GEARMODULE, 3, B01000100);
        lc.setRow(GEARMODULE, 4, B01000100);
        lc.setRow(GEARMODULE, 5, B01000100);
        lc.setRow(GEARMODULE, 6, B01000100);
        lc.setRow(GEARMODULE, 7, B00111000);
        break;
      */
#ifdef MIRROR
    case 1:
      lc.setRow(GEARMODULE, 0, B00010000);
      lc.setRow(GEARMODULE, 1, B00110000);
      lc.setRow(GEARMODULE, 2, B00010000);
      lc.setRow(GEARMODULE, 3, B00010000);
      lc.setRow(GEARMODULE, 4, B00010000);
      lc.setRow(GEARMODULE, 5, B00010000);
      lc.setRow(GEARMODULE, 6, B00010000);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 2:
      lc.setRow(GEARMODULE, 0, B00111000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 2, B00000100);
      lc.setRow(GEARMODULE, 3, B00000100);
      lc.setRow(GEARMODULE, 4, B00001000);
      lc.setRow(GEARMODULE, 5, B00010000);
      lc.setRow(GEARMODULE, 6, B00100000);
      lc.setRow(GEARMODULE, 7, B01111100);
      break;
    case 3:
      lc.setRow(GEARMODULE, 0, B00111000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 2, B00000100);
      lc.setRow(GEARMODULE, 3, B00011000);
      lc.setRow(GEARMODULE, 4, B00000100);
      lc.setRow(GEARMODULE, 5, B00000100);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 4:
      lc.setRow(GEARMODULE, 0, B00000100);
      lc.setRow(GEARMODULE, 1, 12);
      lc.setRow(GEARMODULE, 2, B00010100);
      lc.setRow(GEARMODULE, 3, B00100100);
      lc.setRow(GEARMODULE, 4, B01000100);
      lc.setRow(GEARMODULE, 5, B01111100);
      lc.setRow(GEARMODULE, 6, B00000100);
      lc.setRow(GEARMODULE, 7, B00000100);
      break;
    case 5:
      lc.setRow(GEARMODULE, 0, B01111100);
      lc.setRow(GEARMODULE, 1, B01000000);
      lc.setRow(GEARMODULE, 2, B01000000);
      lc.setRow(GEARMODULE, 3, B01111000);
      lc.setRow(GEARMODULE, 4, B00000100);
      lc.setRow(GEARMODULE, 5, B00000100);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 6:
      lc.setRow(GEARMODULE, 0, B00111000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 2, B01000000);
      lc.setRow(GEARMODULE, 3, B01111000);
      lc.setRow(GEARMODULE, 4, B01000100);
      lc.setRow(GEARMODULE, 5, B01000100);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 7:
      lc.setRow(GEARMODULE, 0, B01111100);
      lc.setRow(GEARMODULE, 1, B00000100);
      lc.setRow(GEARMODULE, 2, B00000100);
      lc.setRow(GEARMODULE, 3, B00001000);
      lc.setRow(GEARMODULE, 4, B00010000);
      lc.setRow(GEARMODULE, 5, B00100000);
      lc.setRow(GEARMODULE, 6, B00100000);
      lc.setRow(GEARMODULE, 7, B00100000);
      break;
    case 8:
      lc.setRow(GEARMODULE, 0, B00111000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 2, B01000100);
      lc.setRow(GEARMODULE, 3, B00111000);
      lc.setRow(GEARMODULE, 4, B01000100);
      lc.setRow(GEARMODULE, 5, B01000100);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 9:
      lc.setRow(GEARMODULE, 0, B00111000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 2, B01000100);
      lc.setRow(GEARMODULE, 3, B01000100);
      lc.setRow(GEARMODULE, 4, B00111100);
      lc.setRow(GEARMODULE, 5, B00000100);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 7, B00111000);
      break;
    case 0:
      lc.setRow(GEARMODULE, 0, B11000110);
      lc.setRow(GEARMODULE, 1, B11100110);
      lc.setRow(GEARMODULE, 2, B11110110);
      lc.setRow(GEARMODULE, 3, B11011110);
      lc.setRow(GEARMODULE, 4, B11001110);
      lc.setRow(GEARMODULE, 5, B11000110);
      lc.setRow(GEARMODULE, 6, B11000110);
      lc.setRow(GEARMODULE, 7, B00000000);
      break;
    case 'r':
      lc.setRow(GEARMODULE, 0, B11111100);
      lc.setRow(GEARMODULE, 1, B01100110);
      lc.setRow(GEARMODULE, 2, B01100110);
      lc.setRow(GEARMODULE, 3, B01111100);
      lc.setRow(GEARMODULE, 4, B01101100);
      lc.setRow(GEARMODULE, 5, B01100110);
      lc.setRow(GEARMODULE, 6, B11100110);
      lc.setRow(GEARMODULE, 7, B00000000);
      reverse = false;
      break;
#else
    case 1:
      lc.setRow(GEARMODULE, 7, B00010000);
      lc.setRow(GEARMODULE, 6, B00011000);
      lc.setRow(GEARMODULE, 5, B00010000);
      lc.setRow(GEARMODULE, 4, B00010000);
      lc.setRow(GEARMODULE, 3, B00010000);
      lc.setRow(GEARMODULE, 2, B00010000);
      lc.setRow(GEARMODULE, 1, B00010000);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 2:
      lc.setRow(GEARMODULE, 7, B00111000);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 5, B01000000);
      lc.setRow(GEARMODULE, 4, B00100000);
      lc.setRow(GEARMODULE, 3, B00010000);
      lc.setRow(GEARMODULE, 2, B00001000);
      lc.setRow(GEARMODULE, 1, B00000100);
      lc.setRow(GEARMODULE, 0, B01111100);
      break;
    case 3:
      lc.setRow(GEARMODULE, 7, B00111000);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 5, B01000000);
      lc.setRow(GEARMODULE, 4, B00110000);
      lc.setRow(GEARMODULE, 3, B01000000);
      lc.setRow(GEARMODULE, 2, B01000000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 4:
      lc.setRow(GEARMODULE, 7, B01000000);
      lc.setRow(GEARMODULE, 6, B01100000);
      lc.setRow(GEARMODULE, 5, B01010000);
      lc.setRow(GEARMODULE, 4, B01001000);
      lc.setRow(GEARMODULE, 3, B01000100);
      lc.setRow(GEARMODULE, 2, B01111100);
      lc.setRow(GEARMODULE, 1, B01000000);
      lc.setRow(GEARMODULE, 0, B01000000);
      break;
    case 5:
      lc.setRow(GEARMODULE, 7, B01111100);
      lc.setRow(GEARMODULE, 6, B00000100);
      lc.setRow(GEARMODULE, 5, B00000100);
      lc.setRow(GEARMODULE, 4, B00111100);
      lc.setRow(GEARMODULE, 3, B01000000);
      lc.setRow(GEARMODULE, 2, B01000000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 6:
      lc.setRow(GEARMODULE, 7, B00111000);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 5, B00000100);
      lc.setRow(GEARMODULE, 4, B00111100);
      lc.setRow(GEARMODULE, 3, B01000100);
      lc.setRow(GEARMODULE, 2, B01000100);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 7:
      lc.setRow(GEARMODULE, 7, B01111100);
      lc.setRow(GEARMODULE, 6, B01000000);
      lc.setRow(GEARMODULE, 5, B01000000);
      lc.setRow(GEARMODULE, 4, B00100000);
      lc.setRow(GEARMODULE, 3, B00010000);
      lc.setRow(GEARMODULE, 2, B00001000);
      lc.setRow(GEARMODULE, 1, B00001000);
      lc.setRow(GEARMODULE, 0, B00001000);
      break;
    case 8:
      lc.setRow(GEARMODULE, 7, B00111000);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 5, B01000100);
      lc.setRow(GEARMODULE, 4, B00111000);
      lc.setRow(GEARMODULE, 3, B01000100);
      lc.setRow(GEARMODULE, 2, B01000100);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 9:
      lc.setRow(GEARMODULE, 7, B00111000);
      lc.setRow(GEARMODULE, 6, B01000100);
      lc.setRow(GEARMODULE, 5, B01000100);
      lc.setRow(GEARMODULE, 4, B01000100);
      lc.setRow(GEARMODULE, 3, B01111000);
      lc.setRow(GEARMODULE, 2, B01000000);
      lc.setRow(GEARMODULE, 1, B01000100);
      lc.setRow(GEARMODULE, 0, B00111000);
      break;
    case 0:
      lc.setRow(GEARMODULE, 7, B11000110);
      lc.setRow(GEARMODULE, 6, B11001110);
      lc.setRow(GEARMODULE, 5, B11011110);
      lc.setRow(GEARMODULE, 4, B11110110);
      lc.setRow(GEARMODULE, 3, B11100110);
      lc.setRow(GEARMODULE, 2, B11000110);
      lc.setRow(GEARMODULE, 1, B11000110);
      lc.setRow(GEARMODULE, 0, B00000000);
      break;
    case 'r':
      lc.setRow(GEARMODULE, 7, B01111110);
      lc.setRow(GEARMODULE, 6, B11001100);
      lc.setRow(GEARMODULE, 5, B11001100);
      lc.setRow(GEARMODULE, 4, B01111100);
      lc.setRow(GEARMODULE, 3, B01101100);
      lc.setRow(GEARMODULE, 2, B11001100);
      lc.setRow(GEARMODULE, 1, B11001110);
      lc.setRow(GEARMODULE, 0, B00000000);
      reverse = false;
      break;
#endif
  }
}

void CalcRPMRange() {
  rpmrange = rpmmax / 100 * rpmpercent ; //divide before otherwise, could overflow
  rpmmin = rpmmax - rpmrange;
  ledweight = (rpmrange / NUMPIXELS);
  //EEPROM.updateByte(24, rpmmax);
#if defined DEBUG
  Serial.print(F("MAX RPM    = ")); Serial.println(rpmmax);
  Serial.print(F("MIN RPM    = ")); Serial.println(rpmmin);
  Serial.print(F("CAR RPM    = ")); Serial.println(carrpm);
  Serial.print(F("RPM PCT    = ")); Serial.println(rpmpercent);
  Serial.print(F("RPM RANGE  = ")); Serial.println(rpmrange);
  Serial.print(F("LED INTERV = ")); Serial.println(ledweight);
#endif
}


void handlePreviousState()
{
  //  unsigned int i;
  //  word leds=0;
  switch (state)
  {
    case GOT_RPMLEARN:
      if (currentValue == 1) {
        rpmlearn = 1;
      }
      else {
        rpmlearn = 0;
      }
      EEPROM.updateByte(22, rpmlearn);
#if defined DEBUG
      Serial.print(F("RPM Learn = ")); Serial.println(rpmlearn);
#endif
      break;
    case GOT_INTENSITY:
      intensity = currentValue;
      for (int address = 0; address < lc.getDeviceCount(); address++) {
        /* Set the brightness to a wanted value */
        lc.setIntensity(address, intensity);
      }
      EEPROM.updateByte(18, intensity);
      break;
    case GOT_MAXRPM:
      processMaxRPM(currentValue);
      break;
    case GOT_RPMPCT:
      rpmpercent = currentValue;
      CalcRPMRange();
      EEPROM.updateByte(14, rpmpercent);
#if defined DEBUG
      Serial.print(F("RPM Percent = ")); Serial.println(rpmpercent);
#endif
      break;
    case GOT_RPMREDLEDS:
      rpmredleds = currentValue;
      EEPROM.updateByte(10, rpmredleds);
#if defined DEBUG
      Serial.print(F("RPM RED LEDS = ")); Serial.println(rpmredleds);
#endif
      break;
    case GOT_RPMORANGELEDS:
      rpmorangeleds = currentValue;
      EEPROM.updateByte(12, rpmorangeleds);
#if defined DEBUG
      Serial.print(F("RPM ORANGE LEDS = ")); Serial.println(rpmorangeleds);
#endif
      break;
    case GOT_SPEEDMULT:
      speedmult = currentValue;
      EEPROM.updateByte(16, speedmult);
#if defined DEBUG
      Serial.print(F("SPEED MULT = ")); Serial.println(speedmult);
#endif
      break;
    case GOT_R:
      processRPM(currentValue);
#if defined DEBUG
      CalcRPMRange();
#endif
      if (carrpm > rpmmin) {
        for ( byte led = NUMPIXELS; led >= 1; led--) {
          if (carrpm - rpmmin >= led * ledweight ) {
            if (led > NUMPIXELS - rpmredleds) {
#if defined DEBUG
              Serial.print(F("LED ")); Serial.print(led); Serial.println(F(" is RED "));
#endif
#if defined REVERSERPMLEDS
              pixels.setPixelColor(NUMPIXELS - led, pixels.Color(intensity + 1 / 2, 0, 0)); // Moderately red color.
#else
              pixels.setPixelColor(led - 1, pixels.Color(intensity + 1 / 2, 0, 0)); // Moderately red color.
#endif
            }
            else {
              if (led > NUMPIXELS - rpmredleds - rpmorangeleds ) {
#if defined DEBUG
                Serial.print(F("LED ")); Serial.print(led); Serial.println(F(" is ORANGE "));
#endif
#if defined REVERSERPMLEDS
                pixels.setPixelColor(NUMPIXELS - led, pixels.Color(intensity + 3, intensity + 1 / 2, 0)); // Moderately orange color.
#else
                pixels.setPixelColor(led - 1, pixels.Color(intensity + 3, intensity + 1 / 2, 0)); // Moderately orange color.
#endif
              }
              else {
#if defined DEBUG
                Serial.print(F("LED ")); Serial.print(led); Serial.println(F(" is GREEN "));
#endif
#if defined REVERSERPMLEDS
#else
#endif
#if defined REVERSERPMLEDS
                pixels.setPixelColor(NUMPIXELS - led, pixels.Color(0, intensity, 0)); // Moderately green color.
#else
                pixels.setPixelColor(led - 1, pixels.Color(0, intensity, 0)); // Moderately green color.
#endif
              }
            }
          }
          else {
#if defined REVERSERPMLEDS
            pixels.setPixelColor(NUMPIXELS - led, pixels.Color(0, 0, 0)); //clear led
#else
            pixels.setPixelColor(led - 1, pixels.Color(0, 0, 0)); //clear led
#endif

#if defined DEBUG
            Serial.print(F("LED ")); Serial.print(led); Serial.println(F(" is OFF "));
#endif
          }
        }
      }
      else {
#if defined DEBUG
        Serial.println(F("RPM too weak. All OFF "));
#endif
        for (int led = 1; led <= NUMPIXELS; led++) {
          pixels.setPixelColor(led - 1, pixels.Color(0, 0, 0)); //clear led
        }
      }
      pixels.show(); // This sends the updated pixel color to the hardware.
      break;
    case GOT_A:
      processA(currentValue);
      break;
    case GOT_B:
      processB(currentValue);
      break;
    case GOT_C:
      processC(currentValue);
      break;
    case GOT_D:
      processD(currentValue);
      break;
    case GOT_E:
      processE(currentValue);
      break;
    case GOT_F:
      processF(currentValue);
      break;
    case GOT_H:
      processH(currentValue);
      break;
    case GOT_I:
      processI(currentValue);
      break;
    case GOT_G:
      processGear(currentValue);
      break;
    case GOT_START:
      processStart(currentValue);
      break;
    case GOT_STOP:
      processStop(currentValue);
      break;
    case NONE:
      break;
  }  // end of switch
  currentValue = 0;
}  // end of handlePreviousState

void processIncomingByte(const byte c)
{
  if (isdigit(c))
  {
    currentValue *= 10;
    currentValue += c - '0';
  }  // end of digit
  else {
    // The end of the number signals a state change
    if (c != '-') {
      handlePreviousState();  //we need to manage minus sign for negative gear, meaning reverse speed
    }
    // set the new state, if we recognize it
    switch (c)
    {
      case '-':
#ifdef DEBUG
        Serial.println(F("Minus car for reverse gear"));
#endif
        reverse = true;
        state = GOT_G;
        break;
      case 'G':
        state = GOT_G;
        break;
      case 'T':
        state = GOT_MAXRPM;
        break;
      case 'R':
        state = GOT_R;
        break;
      case 'A':
        state = GOT_A;
        break;
      case 'B':
        state = GOT_B;
        break;
      case 'C':
        state = GOT_C;
        break;
      case 'D':
        state = GOT_D;
        break;
      case 'E':
        state = GOT_E;
        break;
      case 'F':
        state = GOT_F;
        break;
      case 'H':
        state = GOT_H;
        break;
      case 'I':
        state = GOT_I;
        break;
      case 'Z':
        state = GOT_STOP;
        break;
      case 'K':
        state = GOT_START;
        break;
      case 'L':
        state = GOT_RPMPCT;
        break;
      case 'N':
        state = GOT_RPMREDLEDS;
        break;
      case 'M':
        state = GOT_RPMORANGELEDS;
        break;
      case 'Y':
        state = GOT_INTENSITY;
        break;
      case 'S':
        state = GOT_SPEEDMULT;
        break;
      case 'U':
        state = GOT_RPMLEARN;
        break;
      default:
        state = NONE;
        break;
    }  // end of switch on incoming byte
  } // end of not digit

} // end of processIncomingByte


void setup()
{
  //  unsigned int i;
  //  word leds = 0;

  Serial.begin(115200);
  state = NONE;
  delay(1000);
  rpmredleds = EEPROM.readByte(10); // number of leds that will be red
  rpmorangeleds = EEPROM.readByte(12); // number of leds that will be red
  rpmpercent = EEPROM.readByte(14); // Range to light leds
  speedmult = EEPROM.readByte(16);
  intensity = EEPROM.readByte(18);
  rpmlearn = EEPROM.readByte(22);

  if (intensity > 15 || intensity < 1) {
    intensity = 4;
  }
  CalcRPMRange();
  Serial.println(F("**** DASHX2 v1.21 ****"));
  Serial.print(F("RED LEDS    = ")); Serial.println(rpmredleds);
  Serial.print(F("ORANGE LEDS = ")); Serial.println(rpmorangeleds);
  Serial.print(F("RPMPERCENT  = ")); Serial.println(rpmpercent);
  Serial.print(F("SPEED MULT  = ")); Serial.println(speedmult);
  Serial.print(F("INTENSITY   = ")); Serial.println(intensity);
  Serial.print(F("RPMLEARN    = ")); Serial.println(rpmlearn);
  reverse = false;
  pinMode(RPMRESET, INPUT_PULLUP);
  int devices = lc.getDeviceCount();
  //we have to init all devices in a loop
  for (int address = 0; address < devices; address++) {
#if defined DEBUG
    Serial.print(F("INIT MAX7219 module ")); Serial.println(address);
#endif
    /*The MAX72XX is in power-saving mode on startup*/
    lc.shutdown(address, false);
    /* Set the brightness to a medium values */
    lc.setIntensity(address, intensity);
    /* and clear the display */
    lc.clearDisplay(address);
  }
  pixels.begin();
  for (int led = 1; led <= NUMPIXELS; led++) {
    pixels.setPixelColor(led - 2, pixels.Color(0, 0, 0)); //clear led
    pixels.setPixelColor(led - 1, pixels.Color(0, 5, 0)); //greeen led
    pixels.show();
    delay(20);
  }
  for (int led = NUMPIXELS; led >= 0; led--) {
    pixels.setPixelColor(led + 1, pixels.Color(0, 0, 0)); //clear led
    pixels.setPixelColor(led - 1, pixels.Color(5, 0, 0)); //red led
    pixels.show();
    delay(20);
  }
  for (int led = 1; led <= NUMPIXELS; led++) {
    pixels.setPixelColor(led - 2, pixels.Color(0, 0, 0)); //clear led
    pixels.setPixelColor(led - 1, pixels.Color(0, 0, 5)); //blue led
    pixels.show();
    delay(20);
  }
  pixels.clear();
  pixels.show();
  for (int led = 0; led <= 8; led++) {
    lc.setRow(GEARMODULE, 7, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 6, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 5, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 4, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 3, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 2, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 1, 1 << led); delay(DELAY);
    lc.setRow(GEARMODULE, 0, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 7, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 6, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 5, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 4, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 3, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 2, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 1, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE1, 0, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 7, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 6, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 5, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 4, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 3, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 2, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 1, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE2, 0, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 7, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 6, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 5, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 4, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 3, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 2, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 1, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE3, 0, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 7, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 6, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 5, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 4, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 3, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 2, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 1, 1 << led); delay(DELAY);
    lc.setRow(SEGMODULE4, 0, 1 << led); delay(DELAY);
  }
  processA(0);
  processB(0);
  processC(0);
  processD(0);
  processE(0);
  processF(0);
  processH(0);
  processI(0);
  processGear(0);
  pixels.setPixelColor(0, pixels.Color(0, 5, 0));
  pixels.setPixelColor(NUMPIXELS-1, pixels.Color(0, 5, 0));
  pixels.show();
}  // end of setup

void loop()
{
  //int delayval = 500; // delay for half a second
  //  buttons = btn4; //For testing purpose
  while (1 == 1) {
    while (Serial.available()) processIncomingByte(Serial.read());
    if ((digitalRead(RPMRESET) == LOW))
    {
      rpmmax = 1000;
#if defined DEBUG
      Serial.println(F("RPMRESET activated. Max=1000"));
#endif
    }
  }
}
