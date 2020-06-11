This work is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
http://creativecommons.org/licenses/by-nc-sa/4.0/legalcode
Initial credits go to Nick Gammon for serial state machine

Philosophy:
1- decode serial communication
2- Manage update of global value including controls and math if needed
3- Update display using handlePreviousState and config in EEPROM


Serial/I2C syntax: [command letter][decimal number]
Ax     : Value to dislay in A zone
Bx     : Value to dislay in B zone
Cx     : Value to dislay in C zone
Dx     : Value to dislay in D zone
<<<<<<< HEAD
Ex     : Value to dislay in E zone
Fx     : Value to dislay in F zone
Hx     : Value to dislay in H zone
Ix     : Value to dislay in I zone

=======
>>>>>>> 0fffd7f31ca1e04dc2e2931baced0c958b9ab284
Gx	   : Gear (- for reverse, 0 for neutral)
Rx     : RPM update (decimal notation)
Tx     : Max RPM
Lx     : Display only last x% of RPM on RGB LED ribbon (ex: 1600 max, with L=10 will light LEDS from 1440 to 1600)
Ux     : RPM autolearn (on=1,off=0).Usefull when game provides data itself. Otherwise we will learn from values received along time
Nx	   : number of red LEDs
Mx	   : number of orange LEDs
Sx     : Enable(1)/Disable(0) speed multiplier
Yx     : LED intensity (1-8)
Z0     : clear and stop display
K0     : start display module

The command will be processed when a new one starts. For example, RPM will be updated on the module once a new command 
such as S will start
Serial data to start with max luminosity set (5), speed=95, RPM=4250, gear=2, lap=3, position=12 (last caracter is to finish
processing of RPM, can be anything except a digit)

Sample INIT string, to make sure everything is initialized at least once in EEPROM correctly:
T9600L20N3M4S0Y3U0K0 : define RPM to dipsplay between 8991 and 9600 (20%), no RPM learn, intensity = 3, 3 RED LEDS, 4 Orange LEDS
L40N3M4S0Y3U1K0 : define RPM to dipsplay (40%), RPM learn, intensity = 3, 3 RED LEDS, 4 Orange LEDS


Sample Game string:
T1600L100N3M4S0R1600G2A1B2C3D4G2
Y2T1600L100N2M3S0R800G2A1B2C3D4G2
T1600R99
T1600N2M3L20R1440R1280R1600

If gear is negative, the reverse is assumed and 'R' is displayed
If gear is 0, neutral is assumed and 'N' is displayed

Power usage: A=B=C=D=E=F=H=I=8888 RPM=all red GEAR= N (34 LEDS)
Y1A8888B8888C8888D8888E8888F8888H8888I8888G0T1600R1600G0
intensity=1  => 0.15 ma
intensity=4  => 0.32 ma
intensity=8  => 0.45 ma
intensity=15 => 0.65 ma

Arduino UNO / Genuino wiring: 
	+----[PWR]-------------------| USB |--+
	|                            +-----+  |
	|         GND/RST2  [ ][ ]            |
	|       MOSI2/SCK2  [ ][ ]  A5/SCL[ ] |   C5
	|          5V/MISO2 [ ][ ]  A4/SDA[ ] |   C4
	|                             AREF[ ] |
	|                              GND[ ] |
	| [ ]N/C                    SCK/13[ ] |   B5
	| [ ]IOREF                 MISO/12[ ] |   . MAX7219 CS
	| [ ]RST                   MOSI/11[X]~|   . MAX7219 CLK
	| [ ]3V3    +---+               10[X]~|   . MAX7219 DATA
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

Arduino NANO layout:

					  +-----+
		 +------------| USB |------------+
		 |            +-----+            |
	B5   | [ ]D13/SCK        MISO/D12[ ] |   B4 - MAX7219 CS
		 | [ ]3.3V           MOSI/D11[ ]~|   B3 - MAX7219 CLK
		 | [ ]V.ref     ___    SS/D10[ ]~|   B2 - MAX7219 DATA
	C0   | [ ]A0       / N \       D9[ ]~|   B1
	C1   | [ ]A1      /  A  \      D8[ ] |   B0
	C2   | [ ]A2      \  N  /      D7[ ] |   D7
	C3   | [ ]A3       \_0_/       D6[ ]~|   D6 - DIN RGB LEDS
	C4   | [ ]A4/SDA               D5[ ]~|   D5 - RPM reset switch (short to GND)
	C5   | [ ]A5/SCL               D4[ ] |   D4
		 | [ ]A6              INT1/D3[ ]~|   D3
		 | [ ]A7              INT0/D2[ ] |   D2
		 | [ ]5V                  GND[ ] |     
	C6   | [ ]RST                 RST[ ] |   C6
		 | [ ]GND   5V MOSI GND   TX1[ ] |   D0
		 | [ ]Vin   [ ] [ ] [ ]   RX1[ ] |   D1
		 |          [ ] [ ] [ ]          |
		 |          MISO SCK RST         |
		 | NANO-V3                       |
		 +-------------------------------+
		 
http://busyducks.com/ascii-art-arduinos
http://busyducks.com/ascii-art-arduinos
