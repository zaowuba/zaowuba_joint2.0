/*
* Moisture Led.ino
* A demo for ChaiHuo ZaoWuBa Demo T14014
*
* Copyright (c) 2015 Seeed Technology Inc.
* Auther     : lambor.Fang
* Create Time: May 2015
* Change Log :
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

/**
 * WatchDog class
 */
#include <avr/wdt.h>

#define VERSION "joint v2.0"
#define NAME    "switch vibration"
#define SKU     "110060103"

class WatchDog
{
  public:
    //initial watchdog timeout
    WatchDog(long timeout = 2000){
      _timeout = timeout;
    }

    //method
    void watchdogSetup(void){
      cli();
      wdt_reset();
      MCUSR &= ~(1<<WDRF);
      WDTCSR = (1<<WDCE) | (1<<WDE);
      WDTCSR = (1<<WDIE) | (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (0<<WDP0);
      sei();
    }

    void doggieTickle(void){
      ResetTime = millis();
    }

    //reset
    void(* resetFunc) (void) = 0;

    //parameters
    unsigned long ResetTime;
    volatile bool  Flg_Power;
    long _timeout;

};

WatchDog WTD;

ISR(WDT_vect)
{
  if(millis() - WTD.ResetTime > WTD._timeout)
  {
    WTD.doggieTickle();
    WTD.resetFunc();
  }
}
/******************* End of WatchDog ********************/

/**
 *  Class TimerOne
 */
#include <avr/io.h>
#include <avr/interrupt.h>

#define RESOLUTION 65536    // Timer1 is 16 bit

class TimerOne
{
  public:

    // properties
    unsigned int pwmPeriod;
    unsigned char clockSelectBits;
	  char oldSREG;					// To hold Status Register while ints disabled

    // methods
    void initialize(long microseconds=1000000)
    {
      TCCR1A = 0;                 // clear control register A
      TCCR1B = _BV(WGM13);        // set mode 8: phase and frequency correct pwm, stop the timer
      setPeriod(microseconds);
    }

    void start()
    {
      unsigned int tcnt1;

      TIMSK1 &= ~_BV(TOIE1);        // AR added
      GTCCR |= _BV(PSRSYNC);   		// AR added - reset prescaler (NB: shared with all 16 bit timers);

      oldSREG = SREG;				// AR - save status register
      cli();						// AR - Disable interrupts
      TCNT1 = 0;
      SREG = oldSREG;          		// AR - Restore status register
      resume();
      do {	// Nothing -- wait until timer moved on from zero - otherwise get a phantom interrupt
      oldSREG = SREG;
      cli();
      tcnt1 = TCNT1;
      SREG = oldSREG;
      } while (tcnt1==0);

    //  TIFR1 = 0xff;              		// AR - Clear interrupt flags
    //  TIMSK1 = _BV(TOIE1);              // sets the timer overflow interrupt enable bit
    }

    void stop()
    {
      TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12));          // clears all clock selects bits
    }

    void restart()
    {
      start();
    }

	  void resume()
    {
      TCCR1B |= clockSelectBits;
    }

	  unsigned long read()        //returns the value of the timer in microseconds
    {                           //rember! phase and freq correct mode counts up to then down again
      unsigned long tmp;				// AR amended to hold more than 65536 (could be nearly double this)
      unsigned int tcnt1;				// AR added

      oldSREG= SREG;
        cli();
        tmp=TCNT1;
      SREG = oldSREG;

      char scale=0;
      switch (clockSelectBits)
      {
      case 1:// no prescalse
        scale=0;
        break;
      case 2:// x8 prescale
        scale=3;
        break;
      case 3:// x64
        scale=6;
        break;
      case 4:// x256
        scale=8;
        break;
      case 5:// x1024
        scale=10;
        break;
      }

      do {	// Nothing -- max delay here is ~1023 cycles.  AR modified
        oldSREG = SREG;
        cli();
        tcnt1 = TCNT1;
        SREG = oldSREG;
      } while (tcnt1==tmp); //if the timer has not ticked yet

      //if we are counting down add the top value to how far we have counted down
      tmp = (  (tcnt1>tmp) ? (tmp) : (long)(ICR1-tcnt1)+(long)ICR1  );		// AR amended to add casts and reuse previous TCNT1
      return ((tmp*1000L)/(F_CPU /1000L))<<scale;
    }

    void pwm(char pin, int duty, long microseconds=-1)
    {
      if(microseconds > 0) setPeriod(microseconds);
      if(pin == 1 || pin == 9) {
        DDRB |= _BV(PORTB1);                                   // sets data direction register for pwm output pin
        TCCR1A |= _BV(COM1A1);                                 // activates the output pin
      }
      else if(pin == 2 || pin == 10) {
        DDRB |= _BV(PORTB2);
        TCCR1A |= _BV(COM1B1);
      }
      setPwmDuty(pin, duty);
      resume();			// Lex - make sure the clock is running.  We don't want to restart the count, in case we are starting the second WGM
                    // and the first one is in the middle of a cycle
    }

    void disablePwm(char pin)
    {
      if(pin == 1 || pin == 9)       TCCR1A &= ~_BV(COM1A1);   // clear the bit that enables pwm on PB1
      else if(pin == 2 || pin == 10) TCCR1A &= ~_BV(COM1B1);   // clear the bit that enables pwm on PB2
    }

    void attachInterrupt(void (*isr)(), long microseconds=-1)
    {
      if(microseconds > 0) setPeriod(microseconds);
      isrCallback = isr;                                       // register the user's callback with the real ISR
      TIMSK1 = _BV(TOIE1);                                     // sets the timer overflow interrupt enable bit
      // might be running with interrupts disabled (eg inside an ISR), so don't touch the global state
      //  sei();
      resume();
    }

    void detachInterrupt()
    {
      TIMSK1 &= ~_BV(TOIE1);  // clears the timer overflow interrupt enable bit
															// timer continues to count without calling the isr
    }

    void setPeriod(long microseconds)
    {
      long cycles = (F_CPU / 2000000) * microseconds;                                // the counter runs backwards after TOP, interrupt is at BOTTOM so divide microseconds by 2
      if(cycles < RESOLUTION)              clockSelectBits = _BV(CS10);              // no prescale, full xtal
      else if((cycles >>= 3) < RESOLUTION) clockSelectBits = _BV(CS11);              // prescale by /8
      else if((cycles >>= 3) < RESOLUTION) clockSelectBits = _BV(CS11) | _BV(CS10);  // prescale by /64
      else if((cycles >>= 2) < RESOLUTION) clockSelectBits = _BV(CS12);              // prescale by /256
      else if((cycles >>= 2) < RESOLUTION) clockSelectBits = _BV(CS12) | _BV(CS10);  // prescale by /1024
      else        cycles = RESOLUTION - 1, clockSelectBits = _BV(CS12) | _BV(CS10);  // request was out of bounds, set as maximum

      oldSREG = SREG;
      cli();							// Disable interrupts for 16 bit register access
      ICR1 = pwmPeriod = cycles;                                          // ICR1 is TOP in p & f correct pwm mode
      SREG = oldSREG;

      TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12));
      TCCR1B |= clockSelectBits;
    }

    void setPwmDuty(char pin, int duty)
    {
      unsigned long dutyCycle = pwmPeriod;

      dutyCycle *= duty;
      dutyCycle >>= 10;

      oldSREG = SREG;
      cli();
      if(pin == 1 || pin == 9)       OCR1A = dutyCycle;
      else if(pin == 2 || pin == 10) OCR1B = dutyCycle;
      SREG = oldSREG;
    }

    void (*isrCallback)();
};

TimerOne Timer1;              // preinstantiate

ISR(TIMER1_OVF_vect)          // interrupt service routine that wraps a user defined function supplied by attachInterrupt
{
  Timer1.isrCallback();
}

/*************** End of TimeOne ***************************/

#define DEBUG          0

#define BUTTON         2
#define LIGHT_SENSOR   A0
#define CHRG_LED       A3  //low-level work
#define PWR_HOLD       A1
#define PWR            6   //low-level work
#define KEY            2
#define LED            10
#define OUT_PIN1       3   //normal output pin
#define OUT_PIN2       5
#define IN_PIN1        A5  //normal input pin
#define IN_PIN2        A4

#define SWITCH         A5
#define VIBRATOR       3
#define TIMEUP         45*60  //45 minutes

unsigned long timeCounter = 0;

void setup()
{
  Serial.begin(9600);
  Serial.print("Name: ");
  Serial.println(NAME);
  Serial.print("SKU: ");
  Serial.println(SKU);
  Serial.print("Version: ");
  Serial.println(VERSION);
  delay(100);
  
  WTD.watchdogSetup();
  WTD.doggieTickle();

  //power up
  pinMode(CHRG_LED, OUTPUT);
  digitalWrite(CHRG_LED, LOW);

  pinMode(SWITCH, INPUT);
  digitalWrite(SWITCH, HIGH);
  pinMode(VIBRATOR, OUTPUT);

  pinMode (10,OUTPUT);
  for(int i=0;i<2;i++)
  {
    analogWrite(10,5);
    delay(500);
    analogWrite(10,0);
    delay(500);
    WTD.doggieTickle();
  }
#if DEBUG
  Serial.begin(9600);
#endif
  Timer1.initialize(500000);//timing for 500ms
}

void TimingISR()
{
  timeCounter++;
#if DEBUG
  Serial.println(digitalRead(SWITCH));
#endif
}

void loop()
{
  if(HIGH == digitalRead(SWITCH))
  {
    vibrate(1);
    while(HIGH == digitalRead(SWITCH))
    {
      WTD.doggieTickle();
      delay(50);
    }
  }
  else if(LOW == digitalRead(SWITCH))
  {
    Timer1.attachInterrupt(TimingISR);
    while(LOW == digitalRead(SWITCH))
    {
      if(TIMEUP < timeCounter/2)
      {
        vibrate(1);
      }
      WTD.doggieTickle();
    }
    Timer1.detachInterrupt();
    timeCounter = 0;
  }
  WTD.doggieTickle();
}

void vibrate(float freq)
{
  int ms = (int)freq / 2.0 * 1000;
  digitalWrite(VIBRATOR, HIGH);
  delay(ms);
  digitalWrite(VIBRATOR, LOW);
  delay(ms);
}
