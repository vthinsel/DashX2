DashX2

/*
This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
http://creativecommons.org/licenses/by-nc-sa/4.0/legalcode
Initial credits go to Nick Gammon for serial state machine

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
