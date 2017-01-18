/*
This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
http://creativecommons.org/licenses/by-nc-sa/4.0/legalcode
Initial credits go to Nick Gammon for serial state machine

Version 1.0

Philosophy:
1- decode serial communication
2- Manage update of global value including controls and math if needed
3- Update display using handlePreviousState and config in EEPROM
*/

/*
Serial/I2C syntax: [command letter][decimal number]
// Ax      : Value to dislay in A zone
// Bx      : Value to dislay in B zone
// Cx      : Value to dislay in C zone
// Dx      : Value to dislay in D zone
// Gx	   : Gear (- for reverse, 0 for neutral)
// Rx      : RPM update (decimal notation)
// Tx      : Max RPM
// Exxxx   : lap time. 4 bytes representing  float according to IEEE
// Lx      : Display only last x% of RPM on RGB LED ribbon (ex: 1600 max, with L=10 will light LEDS from 1440 to 1600)
// Ux       : RPM autolearn (on=1,off=0).Usefull when game provides data itself. Otherwise we will learn from values received along time
// Nx	   : number of red LEDs
// Mx	   : number of orange LEDs
// Sx	   : speed multiplier (0=no,1=x3.6). Not used for now
// Yx      : LED intensity (1-8)
// Z0      : clear and stop display
// K0      : start display module
The command will be processed when a new one starts. For example, RPM will be updated on the module once a new command such as S will start
Serial data to start with max luminosity set (5), speed=95, RPM=4250, gear=2, lap=3, position=12 (last caracter is to finish processing of RPM, can be anything except a digit)

INIT string:
T9600L20N3M4S0Y3U0K0 : define RPM to dipsplay between 8991 and 9600 (20%), no RPM learn, intensity = 3, 3 RED LEDS, 4 Orange LEDS

Game string:
A1B2C3D4G0R9400

T1600L100N3M4S0R1600G2A1B2C3D4G2
Y2T1600L100N2M3S0R800G2A1B2C3D4G2
T1600R99
T1600N2M3L20R1440R1280R1600

If gear is negative, the reverse is assumed and 'r' is displayed
If gear is 0, neutral is assumed and 'n' is displayed

Power usage: A=B=C=D=8888 RPM=all green GEAR= N (34 LEDS)
Y1A8888B8888C8888D8888G0T1600R1600G0
intensity=1 => ma
intensity=2 => ma
intensity=3 => ma
intensity=4 => ma
intensity=5 => ma
intensity=6 => ma
intensity=7 => ma
intensity=8 => ma

					+----[PWR]-------------------| USB |--+
					|                            +-----+  |
					|         GND/RST2  [ ][ ]            |
					|       MOSI2/SCK2  [ ][ ]  A5/SCL[ ] |   C5
					|          5V/MISO2 [ ][ ]  A4/SDA[ ] |   C4
					|                             AREF[ ] |
					|                              GND[ ] |
					| [ ]N/C                    SCK/13[ ] |   B5
					| [ ]IOREF                 MISO/12[ ] |   . TM1638 DATA
					| [ ]RST                   MOSI/11[X]~|   . TM1638 CLK
					| [ ]3V3    +---+               10[X]~|   . TM1638 CS
					| [ ]5v    -| A |-               9[X]~|   .
					| [ ]GND   -| R |-               8[ ] |   B0
					| [ ]GND   -| D |-                    |
					| [ ]Vin   -| U |-               7[ ] |   D7
					|          -| I |-               6[X]~|   .   DIN RGB LEDS  
					| [ ]A0    -| N |-               5[ ]~|   .   RPM reset switch (short to GND)
					| [ ]A1    -| O |-               4[ ] |   .
					| [ ]A2     +---+           INT1/3[ ]~|   .
					| [ ]A3                     INT0/2[ ] |   .
					| [ ]A4/SDA  RST SCK MISO     TX>1[ ] |   .
					| [ ]A5/SCL  [ ] [ ] [ ]      RX<0[ ] |   D0
					|            [ ] [ ] [ ]              |
					|  UNO_R3    GND MOSI 5V  ____________/
					\_______________________/
		 
					 http://busyducks.com/ascii-art-arduinos

*/

#define DEBUG
#include <Boards.h>
#include "LedControl.h"
#include <EEPROMex.h>
#include <SoftwareSerial.h>
#include "DashX2.h"
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
#include <avr/power.h>
#endif

#undef _EEPROMEX_DEBUG

//#include <Wire.h>
#undef _EEPROMEX_DEBUG //to prevent false alarms on EEPROM write execeeded
unsigned int valueA;
unsigned int valueB;
unsigned int valueC;
unsigned int valueD;
unsigned int carrpm;           // holds the rpm data (0-65535 size)
unsigned int cargear;          // holds gear value data (0-65535 size)
unsigned int rpmmax = 1000;    // autocalibrating initial RPM max val

byte rpmredleds; // number of leds that will be red
byte rpmorangeleds; // number of leds that will be orange
byte rpmpercent; // Range to light leds
byte speedmult;
byte intensity;
bool reverse;
//String text;

// RPM related stuff
int rpmmin;
int rpmrange;
int ledweight;
int rpmlearn;
// the possible states of the state-machine
typedef enum { NONE, GOT_RPMLEARN, GOT_INTENSITY, GOT_SPEEDMULT, GOT_A, GOT_B, GOT_C, GOT_D, GOT_R, GOT_G, GOT_STOP, GOT_START, GOT_MAXRPM , GOT_RPMPCT, GOT_RPMREDLEDS, GOT_RPMORANGELEDS} states;

// current state-machine state
states state = NONE;
// current partial number
signed int currentValue;
//Data Clock Strobe
LedControl lc = LedControl(12, 11, 10, 3);

#define PIN            6
// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      16
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
	if (carrpm > rpmmax && carrpm < 50000 && rpmlearn == 1) {
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

void printNumberNeg(int v) {
	int ones;
	int tens;
	int hundreds;
	boolean negative;

	if (v < -999 || v > 999)
		return;
	if (v<0) {
		negative = true;
		v = v*-1;
	}
	ones = v % 10;
	v = v / 10;
	tens = v % 10;
	v = v / 10;
	hundreds = v;
	if (negative) {
		//print character '-' in the leftmost column	
		lc.setChar(0, 3, '-', false);
	}
	else {
		//print a blank in the sign column
		lc.setChar(0, 3, ' ', false);
	}
	//Now print the number digit by digit
	lc.setDigit(0, 2, (byte)hundreds, false);
	lc.setDigit(0, 1, (byte)tens, false);
	lc.setDigit(0, 0, (byte)ones, false);
}

void printNumber(int module, int offset, int v) {
	int ones;
	int tens;
	int hundreds;
	int thousands;

	if ( v > 9999)
		return;
	ones = v % 10;
	v = v / 10;
	tens = v % 10;
	v = v / 10;
	hundreds = v % 10 ;
	v = v / 10;
	thousands = v;
	//Now print the number digit by digit
	if (thousands == 0){
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
	if (thousands) { lc.setDigit(module, 3 + offset, (byte)thousands, false); }
	if (hundreds) { lc.setDigit(module, 2 + offset, (byte)hundreds, false); }
	if (tens) { lc.setDigit(module, 1 + offset, (byte)tens, false); }
	if (ones) { lc.setDigit(module, 0 + offset, (byte)ones, true); }
}

void processA(const unsigned int value)
{
#if defined DEBUG
	Serial.print(F("ValueA = ")); Serial.println(value);
#endif
	valueA= value;
	printNumber(0, 4 ,value);
}

void processB(const unsigned int value)
{
#if defined DEBUG
	Serial.print(F("ValueB = ")); Serial.println(value);
#endif
	valueB = value;
	printNumber(0, 0, value);
}

void processC(const unsigned int value)
{
#if defined DEBUG
	Serial.print(F("ValueC = ")); Serial.println(value);
#endif
	valueC = value;
	printNumber(1, 4, value);
}

void processD(const unsigned int value)
{
#if defined DEBUG
	Serial.print(F("ValueD = ")); Serial.println(value);
#endif
	valueD = value;
	printNumber(1, 0, value);
}

void processStop(const unsigned int value)
{// We just clear everything
#if defined DEBUG
	Serial.print(F("Stop = ")); Serial.println(value);
#endif
	int devices = lc.getDeviceCount();
	for (int address = 0;address<devices;address++) {
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
	if (reverse == true) { gear = 'r'; }
	switch (gear) {
	/*
	case 0:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B01000100);
		lc.setRow(2, 3, B01000100);
		lc.setRow(2, 4, B01000100);
		lc.setRow(2, 5, B01000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	*/
	case 1:
		lc.setRow(2, 0, B00010000);
		lc.setRow(2, 1, B00110000);
		lc.setRow(2, 2, B00010000);
		lc.setRow(2, 3, B00010000);
		lc.setRow(2, 4, B00010000);
		lc.setRow(2, 5, B00010000);
		lc.setRow(2, 6, B00010000);
		lc.setRow(2, 7, B00111000);
		break;
	case 2:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B00000100);
		lc.setRow(2, 3, B00000100);
		lc.setRow(2, 4, B00001000);
		lc.setRow(2, 5, B00010000);
		lc.setRow(2, 6, B00100000);
		lc.setRow(2, 7, B01111100);
		break;
	case 3:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B00000100);
		lc.setRow(2, 3, B00011000);
		lc.setRow(2, 4, B00000100);
		lc.setRow(2, 5, B00000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	case 4:
		lc.setRow(2, 0, B00000100);
		lc.setRow(2, 1, 12);
		lc.setRow(2, 2, B00010100);
		lc.setRow(2, 3, B00100100);
		lc.setRow(2, 4, B01000100);
		lc.setRow(2, 5, B01111100);
		lc.setRow(2, 6, B00000100);
		lc.setRow(2, 7, B00000100);
		break;
	case 5:
		lc.setRow(2, 0, B01111100);
		lc.setRow(2, 1, B01000000);
		lc.setRow(2, 2, B01000000);
		lc.setRow(2, 3, B01111000);
		lc.setRow(2, 4, B00000100);
		lc.setRow(2, 5, B00000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	case 6:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B01000000);
		lc.setRow(2, 3, B01111000);
		lc.setRow(2, 4, B01000100);
		lc.setRow(2, 5, B01000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	case 7:
		lc.setRow(2, 0, B01111100);
		lc.setRow(2, 1, B00000100);
		lc.setRow(2, 2, B00000100);
		lc.setRow(2, 3, B00001000);
		lc.setRow(2, 4, B00010000);
		lc.setRow(2, 5, B00100000);
		lc.setRow(2, 6, B00100000);
		lc.setRow(2, 7, B00100000);
		break;
	case 8:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B01000100);
		lc.setRow(2, 3, B00111000);
		lc.setRow(2, 4, B01000100);
		lc.setRow(2, 5, B01000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	case 9:
		lc.setRow(2, 0, B00111000);
		lc.setRow(2, 1, B01000100);
		lc.setRow(2, 2, B01000100);
		lc.setRow(2, 3, B01000100);
		lc.setRow(2, 4, B00111100);
		lc.setRow(2, 5, B00000100);
		lc.setRow(2, 6, B01000100);
		lc.setRow(2, 7, B00111000);
		break;
	case 0:
		lc.setRow(2, 0, B11000110);
		lc.setRow(2, 1, B11100110);
		lc.setRow(2, 2, B11110110);
		lc.setRow(2, 3, B11011110);
		lc.setRow(2, 4, B11001110);
		lc.setRow(2, 5, B11000110);
		lc.setRow(2, 6, B11000110);
		lc.setRow(2, 7, B00000000);
		break;
	case 'r':
		lc.setRow(2, 0, B11111100);
		lc.setRow(2, 1, B01100110);
		lc.setRow(2, 2, B01100110);
		lc.setRow(2, 3, B01111100);
		lc.setRow(2, 4, B01101100);
		lc.setRow(2, 5, B01100110);
		lc.setRow(2, 6, B11100110);
		lc.setRow(2, 7, B00000000);
		reverse = false;
		break;

	}
}

void CalcRPMRange() {
	rpmrange = rpmmax / 100 * rpmpercent ; //divide before otherwise, could overflow
	rpmmin = rpmmax - rpmrange;
	ledweight = (rpmrange / NUMPIXELS);
	EEPROM.updateByte(18, rpmmax);
#if defined DEBUG
	Serial.print(F("MAX RPM    = "));Serial.println(rpmmax);
	Serial.print(F("MIN RPM    = "));Serial.println(rpmmin);
	Serial.print(F("CAR RPM    = "));Serial.println(carrpm);
	Serial.print(F("RPM PCT    = "));Serial.println(rpmpercent);
	Serial.print(F("RPM RANGE  = "));Serial.println(rpmrange);
	Serial.print(F("LED INTERV = "));Serial.println(ledweight);
#endif
}


void handlePreviousState()
{
	unsigned int i;
	word leds=0;
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
		for (int address = 0;address<lc.getDeviceCount();address++) {
			/* Set the brightness to a wanted value */
			lc.setIntensity(address, intensity);
		}
		EEPROM.updateByte(18, intensity);
		break;
	case GOT_MAXRPM:
		processMaxRPM(currentValue);
		CalcRPMRange();
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
			for (int led = 1;led <= NUMPIXELS;led++) {
				if (carrpm- rpmmin >= led *ledweight ) {
					if (led > NUMPIXELS - rpmredleds) {
#if defined DEBUG

						Serial.print(F("LED "));Serial.print(led);Serial.println(F(" is RED "));
#endif
						pixels.setPixelColor(led - 1, pixels.Color(intensity, 0, 0)); // Moderately red color.
					}
					else {
						if (led > NUMPIXELS - rpmredleds - rpmorangeleds ) {
#if defined DEBUG
							Serial.print(F("LED "));Serial.print(led);Serial.println(F(" is ORANGE "));
#endif
							pixels.setPixelColor(led - 1, pixels.Color(intensity + 3, intensity + 1 / 2, 0)); // Moderately orange color.

						}
						else {
#if defined DEBUG
							Serial.print(F("LED "));Serial.print(led);Serial.println(F(" is GREEN "));
#endif
							pixels.setPixelColor(led - 1, pixels.Color(0, intensity, 0)); // Moderately green color.
						}
					}
				}
				else {
					pixels.setPixelColor(led - 1, pixels.Color(0, 0, 0)); //clear led
#if defined DEBUG
					Serial.print(F("LED "));Serial.print(led);Serial.println(F(" is OFF "));
#endif
				}
			}

		}
		else {
#if defined DEBUG
			Serial.println(F("RPM too weak. All OFF "));
#endif
			for (int led = 1;led <= NUMPIXELS;led++) {
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
	case GOT_G:
		processGear(currentValue);
		break;
	case GOT_START:
		processStart(currentValue);
		break;
	case GOT_STOP:
		processStop(currentValue);
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
		if (c != '-') { handlePreviousState(); } //we need to manage minus sign for negative gear, meaning reverse speed
	  // set the new state, if we rectnesognize it
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

/*
float byte2float(byte binary[]) {
	typedef union {
		float val;
		byte binary[4];
	} binaryFloat;
	binaryFloat unionvar;
	unionvar.binary[0] = binary[0];
	unionvar.binary[1] = binary[1];
	unionvar.binary[2] = binary[2];
	unionvar.binary[3] = binary[3];
#if defined DEBUG
	Serial.print("byte2float: ");
	for (char i = 0; i <4; i++) {
		Serial.print(unionvar.binary[i], HEX);Serial.print(" ");
	}
	Serial.print("= ");Serial.println(unionvar.val,4);
#endif
	return unionvar.val;
}

unsigned int byte2uint(byte binary[]) {
	word val;
	val=word(binary[1],binary[0]);
#if defined DEBUG
	Serial.print("byte2uint: ");
	for (char i = 0; i <2; i++) {
		Serial.print(binary[i], HEX);Serial.print(" ");
	}
	Serial.print("= ");Serial.println(val);
#endif
	return (unsigned int)val;
}

int byte2int(byte binary[]) {
	int val;
	val = binary[1] << 8 | binary[0];
#if defined DEBUG
	Serial.print("byte2int: ");
	for (char i = 0; i <2; i++) {
		Serial.print(binary[i], HEX);Serial.print(" ");
	}
	Serial.print("= ");Serial.println(val);
#endif
	return val;
}

*/
void setup()
{
	Serial.begin(115200);
	state = NONE;
	delay(1000);
	rpmredleds = EEPROM.readByte(10); // number of leds that will be red
	rpmorangeleds = EEPROM.readByte(12); // number of leds that will be red
	rpmpercent = EEPROM.readByte(14); // Range to light leds
	speedmult = EEPROM.readByte(16);
	intensity = EEPROM.readByte(18);
	rpmlearn = EEPROM.readByte(22);

	if (intensity > 8 || intensity < 1) {
		intensity = 2;
	}
	CalcRPMRange();
	Serial.println(F("**** DASHX2 v1.0 ****"));
	Serial.print(F("RED LEDS    = "));Serial.println(rpmredleds);
	Serial.print(F("ORANGE LEDS = "));Serial.println(rpmorangeleds);
	Serial.print(F("RPMPERCENT  = "));Serial.println(rpmpercent);
	Serial.print(F("SPEED MULT  = "));Serial.println(speedmult);
	Serial.print(F("INTENSITY   = "));Serial.println(intensity);
	Serial.print(F("RPMLEARN    = "));Serial.println(rpmlearn);
	reverse = false;
	pinMode(RPMRESET, INPUT_PULLUP);

/*
	Wire.begin(8);                // join i2c bus with address #8
	Wire.onReceive(receiveEvent); // register event
*/
/*
	byte vBuffer[4];
	byte sBuffer[2];
	float floatspeed;
	unsigned int uintspeed;
	int intspeed;
	vBuffer[0] = 0x41;
	vBuffer[1] = 0xA0;
	vBuffer[2] = 0x30;
	vBuffer[3] = 0x40;
	floatspeed=byte2float(vBuffer);
	vBuffer[0] = 0x40;
	vBuffer[1] = 0x30;
	vBuffer[2] = 0xA0;
	vBuffer[3] = 0x41;
	floatspeed = byte2float(vBuffer);
	sBuffer[0] = 0xFE;
	sBuffer[1] = 0x00;
	intspeed = byte2int(sBuffer);
	uintspeed = byte2uint(sBuffer);
	sBuffer[0] = 0xff;
	sBuffer[1] = 0xFf;
	intspeed = byte2int(sBuffer);
	uintspeed = byte2uint(sBuffer);
	*/
	
	int devices = lc.getDeviceCount();
	//we have to init all devices in a loop
	for (int address = 0;address<devices;address++) {
		/*The MAX72XX is in power-saving mode on startup*/
		lc.shutdown(address, false);
		/* Set the brightness to a medium values */
		lc.setIntensity(address, intensity);
		/* and clear the display */
		lc.clearDisplay(address);
	}
	pixels.begin();
	pixels.clear();
	pixels.show();
}  // end of setup

void loop()
{
	int delayval = 500; // delay for half a second

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

/*
void receiveEvent(int howMany) {
	while (1 <= Wire.available()) { 
		char c = Wire.read(); // receive byte as a character
#if defined DEBUG
		Serial.print(c);         // print the character
#endif
		switch (c) {
			case 'S': {
				state = GOT_S;
				byte speedbyte[4];
				int i = 0;
				while (Wire.available()) {
					speedbyte[i++] = (byte)Wire.read();
	#if defined DEBUG
					Serial.print(speedbyte[i-1],HEX);         // print the character
	#endif
					  }
#if defined DEBUG
				Serial.println("");
#endif
					currentValue = (int)byte2float(speedbyte);
					//processSpeed(currentValue);
	#if defined DEBUG
					Serial.print("I2C Speed:");Serial.println(currentValue);
	#endif
					handlePreviousState();
					state = NONE;
					break;
			}
			case 'R': {
				state = GOT_R;
				byte rpmbyte[2];
				int i = 0;
				while (Wire.available()) {
					rpmbyte[i++] = (byte)Wire.read();
	#if defined DEBUG
					Serial.print(rpmbyte[i - 1],HEX);         // print the character
	#endif
				}
#if defined DEBUG
				Serial.println("");
#endif
				currentValue = byte2uint(rpmbyte);
				//processRPM((unsigned int)currentValue);
	#if defined DEBUG
				Serial.print("I2C RPM:");Serial.println(currentValue);
	#endif
				handlePreviousState();
				state = NONE;
				break;
			}
			case 'G': {
				state = GOT_G;
				handlePreviousState();
				state = NONE;
				break;
			}
		}

	}
	//int x = Wire.read();    // receive byte as an integer
	// char c = Wire.read();
	//Serial.println(c);         // print the integer
}
*/