/***************************************************************************
 * HappySwitch v2
 * MESA/Boogie Dual Rectifier MIDI controller.
 * Coded & Designed by Pasquale 'sid' Fiorillo - March 2017
 * 
 * Released under GPL v3 License
 * 
 *
 * REGISTERS:
 * #########################################################################
 * REGISTER NAME    #   BIT MEANINGS
 * #########################################################################
 * config           |   [CH3 LED][CH2 LED][CH1 LED][FX][SOLO][VCHB][VCHA][-]
 * PORTD            |   [D7][D6][D5][D4][D3][D2][D1][D0]
 * PORTB            |   [-][-][D13][D12][D11][D10][D9][D8]
 * 
 *
 * INPUT PINS (button):
 * ###################
 * NAME  #  PIN NUMBER
 * ###################
 * CH1   |   9
 * CH2   |  10
 * CH3   |  11
 * ____  |
 * SOLO  |  12
 * FX    |  13
 * 
 * 
 * OUTPUT PINS:
 * #######################
 * NAME      #  PIN NUMBER
 * #######################
 * VCHA      |  2
 * VCHB      |  3
 * SOLO      |  4
 * FX        |  5
 * CH1 LED   |  6
 * CH2 LED   |  7
 * CH3 LED   |  8
 * 
 * FX is "FX bypass". LED ON = FX bypass, LED off = FX on
 * 
 * VCHA/VCHB TRUTH TABLE:
 * ##################################
 * VCHA  #  VCHB  #  CHANNEL SELECTED
 * ##################################
 * 0     |  0     |  -
 * 1     |  0     |  1
 * 0     |  1     |  2
 * 1     |  1     |  3
 * 
 * 
 ***************************************************************************/
#include <EEPROM.h>

#define DEBUG 1
#define RAW_DEBUG 0					// Raw debug can cause timing issue!
#define MIDI_BAUD_RATE 31250		// RX baudrate (MIDI)
#define DEBUG_BAUD_RATE 38400		// TX baudrate (DEBUG text)
#define MIDI_CHANNEL 0x00			// 0x00 = Channel 1

#define CH1_LED_PIN 6
#define CH2_LED_PIN 7
#define CH3_LED_PIN 8

#define CH1_BUTTON_PIN 9
#define CH2_BUTTON_PIN 10
#define CH3_BUTTON_PIN 11
#define SOLO_BUTTON_PIN 12
#define FX_BUTTON_PIN 13

uint8_t programChangeDetected = 0;
uint8_t currentProgram, currentConfig = 0x00;
uint8_t midiByte;

uint8_t lock;

uint8_t config = B00101010;	// Safe config: CH1 on, SOLO off, FX off

#if DEBUG == 1
	uint8_t raw_debug = RAW_DEBUG;
	char debugMessage[256];
	
	void debug() {
		// set DEBUG baud rate
		Serial.begin(DEBUG_BAUD_RATE);
		Serial.println(debugMessage);
		Serial.flush();
		// set MIDI baud rate
		Serial.begin(MIDI_BAUD_RATE);
	}
#endif

void setup() {
	// set 7-2 pins as output without touching 1-0 pins (tx, rx)
	DDRD |= B11111100;
	// set 13-9 pins as input, 8 as output
	DDRB = B00000001;
	
	// Init the serial port with MIDI baud rate
	Serial.begin(31250);
	Serial.flush();
	
	// First boot, reinit all memory slots to default config
	if (EEPROM.read(0xFF) != 0x99) {
		uint8_t i;
		for (i=0x01; i<=0x7F; i++) {
			EEPROM.write(i, config);
		}
		EEPROM.write(0xFF, 0x99);
		
		// feedback
		digitalWrite(CH3_LED_PIN, HIGH);
		delay(1000);
		digitalWrite(CH3_LED_PIN, LOW);
	}
	
	// Blink CH1,2,3 LEDs to give a feedback to the user
	uint8_t led;
	for (led=CH1_LED_PIN; led<=CH3_LED_PIN; led++) {
		digitalWrite(led, HIGH);
		delay(300);
		digitalWrite(led, LOW);
	}
	
	// apply default config
	applyConfig();
	
	#if DEBUG == 1
		sprintf(debugMessage, "Ready");
		debug();
	#endif
}

void loop() {
	uint8_t bitmask = B00011000;
	currentConfig = config;
	lock = 0;
	
	// For CH1,CH2 and CH3 we need to preserve SOLO and FX bits,
	// so we need to use bitmask to protect them.
	// config = config & bitmask | newValue
	
	if (!digitalRead(CH1_BUTTON_PIN)) {
		//config = config & bitmask | B00100100;
		config = config & bitmask | B00100010;
		lock = CH1_BUTTON_PIN;
		#if DEBUG == 1
			sprintf(debugMessage, "CH1 button pressed. Config: 0x%02X", config);
			debug();
		#endif
	} else if (!digitalRead(CH2_BUTTON_PIN)) {
		//config = config & bitmask | B01000010;
		config = config & bitmask | B01000100;
		lock = CH2_BUTTON_PIN;
		#if DEBUG == 1
			sprintf(debugMessage, "CH2 button pressed. Config: 0x%02X", config);
			debug();
		#endif
	} else if (!digitalRead(CH3_BUTTON_PIN)) {
		//config = config & bitmask | B10000110;
		config = config & bitmask | B10000110;
		lock = CH3_BUTTON_PIN;
		#if DEBUG == 1
			sprintf(debugMessage, "CH3 button pressed. Config: 0x%02X", config);
			debug();
		#endif
	} else if (!digitalRead(SOLO_BUTTON_PIN)) {
		lock = SOLO_BUTTON_PIN;
		config ^= B00001000;
		#if DEBUG == 1
			sprintf(debugMessage, "SOLO button pressed. Config: 0x%02X", config);
			debug();
		#endif
	} else if (!digitalRead(FX_BUTTON_PIN)) {
		lock = FX_BUTTON_PIN;
		config ^= B00010000;
		#if DEBUG == 1
			sprintf(debugMessage, "FX button pressed. Config: 0x%02X", config);
			debug();
		#endif
	}
	
	waitUntilButtonReleased();
	
	applyConfig();
}

void waitUntilButtonReleased() {
	while (lock && !digitalRead(lock)) {
		delay(1);
	}
}

void applyConfig() {
	uint8_t bitmask;
	
	// if nothing has changed, do nothing
	if (currentConfig == config) {
		delay(1);
		return;
	}
	
	// Write [CH2 LED][CH1 LED][FX][SOLO][VCHB][VCHA] config bits directly on PORTD
	// we need to preserve last 2 bits (D1,D0 used by tx & rx) using bitmask
	bitmask = B00000011;
	PORTD = PORTD & bitmask | config << 1;
	
	// Write [CH3 LED] config bit directly on PORTB
	// we need to protect any other bits
	bitmask = B11111110;
	PORTB = PORTB & bitmask | config >> 7;
	
	// avoid to store program at boot time
	if (currentProgram != 0x00) {
		EEPROM.write(currentProgram, config);
	}
}

void readConfig() {
	config = EEPROM.read(currentProgram);
	applyConfig();
}

void serialEvent() {
	if (Serial.available()) {
		midiByte = (uint8_t)Serial.read();
		#if DEBUG == 1
			// Ignore active sensing message.
			// This message is intended to be sent repeatedly (every 
			// 300ms) to tell the receiver that a connection is alive. 
			// Use of this message is optional.
			if (raw_debug && midiByte != 0xFE) {
				sprintf(debugMessage, "MIDI RAW DATA: 0x%02X - programChangeDetected is %d", midiByte, programChangeDetected);
				debug();
			}
		#endif
		
		// if we received a program change STATUS byte on my channel
		if (!programChangeDetected && midiByte == 0xC0|MIDI_CHANNEL) {
			programChangeDetected = 1;
			#if DEBUG == 1
				if (raw_debug) {
					sprintf(debugMessage, "Program change detected, waiting for data byte");
					debug();
				}
			#endif
		}
		
		// if there is a DATA byte
		if (programChangeDetected && midiByte >= 0x00 && midiByte <= 0x7F) {
			programChangeDetected = 0;
			#if DEBUG == 1
				sprintf(debugMessage, "Status & Data byte received! Program Change %d detected on MIDI channel %d", midiByte, MIDI_CHANNEL + 1);
				debug();
			#endif
			if (midiByte != currentProgram) {
				currentProgram = midiByte;
				readConfig();
			}
		}
	}
}
