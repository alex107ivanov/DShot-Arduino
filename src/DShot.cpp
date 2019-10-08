#include "Arduino.h"
#include "DShot.h"


// Each item contains the bit of the port
// Pins that are not attached will always be 1
// This is to offload the Off pattern calculation during bit send
static uint8_t dShotBits[16];

// Denote which pin is attached to dShot
static uint8_t dShotPins = 0;


#define NOP	"NOP\n"
#define NOP2 NOP NOP
#define NOP4 NOP2 NOP2
#define NOP8 NOP4 NOP4

/*
	DSHOT600 implementation
	For 16MHz CPU, 
	0: 10 cycle ON, 17 cycle OFF
	1: 20 cycle ON, 7 cycle OFF
	Total 27 cycle each bit
*/
static inline void sendData(){
	noInterrupts();
	asm(		
		// For i = 0 to 15:
		"LDI r23, 0	\n"
		// Set High for every attached pins
		// DSHOT_PORT |= dShotPins;
		"IN r25, %0 \n"
		"_for_loop:			\n"
		"OR r25, %1 \n"
		"OUT %0, r25 \n"
		// Need 4 cycles to turn off, thus need to wait 6 cycles
		NOP8
		NOP8
		// Set Low for low bits only
		//DSHOT_PORT &= dShotBits[i];
		"LD r24, Z+		\n"
		"AND r25, r24	\n"
		"OUT %0, r25	\n"
		// Need 3 cycles to turn off everything, thus wait 7 cycles
		NOP8
		NOP8
		NOP2
		// Turn off everything
		//DSHOT_PORT &= ~dShotPins;
		"AND r25, %2 \n"
		"OUT %0, r25 \n"
		NOP4
		NOP2
		NOP
		// Add to i (tmp_reg)
		"INC r23		\n"
		"CPI r23, 16	\n"
		"BRLO	_for_loop	\n"
		// 7 cycles to next bit (4 to add to i and branch, 2 to turn on), no wait
		:  
		: "I" (_SFR_IO_ADDR(DSHOT_PORT)), "r" (dShotPins), "r" (~dShotPins), "z" (dShotBits)
		: "r25", "r24", "r23"
	);
	interrupts();
}

static boolean timerActive = false;
/*
  Generated by:
  http://www.8bit-era.cz/arduino-timer-interrupts-calculator.html
  1000 Hz Update rate
*/
static void initISR(){
	// TIMER 2 for interrupt frequency 1000 Hz:
	cli(); // stop interrupts
	TCCR2A = 0; // set entire TCCR2A register to 0
	TCCR2B = 0; // same for TCCR2B
	TCNT2  = 0; // initialize counter value to 0
	// set compare match register for 1000 Hz increments
	OCR2A = 249; // = 16000000 / (64 * 1000) - 1 (must be <256)
	// turn on CTC mode
	TCCR2B |= (1 << WGM21);
	// Set CS22, CS21 and CS20 bits for 64 prescaler
	TCCR2B |= (1 << CS22) | (0 << CS21) | (0 << CS20);
	// enable timer compare interrupt
	TIMSK2 |= (1 << OCIE2A);

	timerActive = true;
	for (byte i=0; i<16; i++){
		dShotBits[i] = 0;
		dShotPins = 0;
	}

	sei(); // allow interrupts
}

static boolean isTimerActive(){
  return timerActive;
}

ISR(TIMER2_COMPA_vect){
   sendData();
}

/*
  Prepare data packet, attach 0 to telemetry bit, and calculate CRC
  throttle: 11-bit data
*/
static inline uint16_t createPacket(uint16_t throttle){
  uint8_t csum = 0;
  throttle <<= 1;
  uint16_t csum_data = throttle;
  for (byte i=0; i<3; i++){
    csum ^= csum_data;
    csum_data >>= 4;
  }
  csum &= 0xf;
  return (throttle<<4)|csum;
}

/****************** end of static functions *******************/

DShot::DShot(){
}

void DShot::attach(uint8_t pin){
  this->_packet = 0;
  this->_pinMask = digitalPinToBitMask(pin);
  pinMode(pin, OUTPUT);
  if (!isTimerActive()){
    initISR();
  }
  dShotPins |= this->_pinMask;
}

/*
  Set the throttle value and prepare the data packet and store
  throttle: 11-bit data
*/
uint16_t DShot::setThrottle(uint16_t throttle){
  this->_throttle = throttle;

  // TODO: This part can be further optimized when combine with create packet
  this->_packet = createPacket(throttle);
  uint16_t mask = 0x8000;
  for (byte i=0; i<16; i++){
    if (this->_packet & mask)
      dShotBits[i] |= this->_pinMask;
    else
      dShotBits[i] &= ~(this->_pinMask);
    mask >>= 1;
  }
  return _packet;
}
