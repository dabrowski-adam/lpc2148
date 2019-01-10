/******************************************************************************
 *
 * Copyright:
 *    (C) 2000 - 2007 Embedded Artists AB
 *
 * Description:
 *    Main program for LPC2148 Education Board test program
 *
 *****************************************************************************/


#include "../pre_emptive_os/api/osapi.h"
#include "../pre_emptive_os/api/general.h"
#include <printf_P.h>
#include <ea_init.h>
#include <lpc2xxx.h>
#include <consol.h>
#include "i2c.h" // May not be needed because eeprom already includes it
#include "eeprom.h"
#include "key.h"
#include "usb/lpc_usb.h"
#include "usb/lpc_hid.h"

#define PROC1_STACK_SIZE 2048
#define PROC2_STACK_SIZE 2048
#define PROC3_STACK_SIZE 2048
#define PROC4_STACK_SIZE 2048
#define PROC5_STACK_SIZE 2048
#define INIT_STACK_SIZE  400

static tU8 proc1Stack[PROC1_STACK_SIZE];
static tU8 proc2Stack[PROC2_STACK_SIZE];
static tU8 proc3Stack[PROC3_STACK_SIZE];
static tU8 proc4Stack[PROC4_STACK_SIZE];
static tU8 proc5Stack[PROC5_STACK_SIZE];
static tU8 initStack[INIT_STACK_SIZE];
static tU8 pid1;
static tU8 pid2;
static tU8 pid3;
static tU8 pid4;
static tU8 pid5;


static void proc1(void* arg);
static void proc2(void* arg);
static void proc3(void* arg);
static void proc4(void* arg);
static void proc5(void* arg);
static void initProc(void* arg);

void testLedMatrix(void);
void testLcd(void);
void testMotor(void);
void testRGB(void);
void testI2C(void);
//void testMMC(void);
void testAdc(void);
tU8  testXBee(void);
void messageOnLCD(char *str, tU8 important);
void lightRGB(tU8 r, tU8 g, tU8 b);

tU8 xbeePresent;
volatile tU32 msClock = 0;
extern char startupSound[];

tU8 alarm[6] = { 128, 255, 255, 128, 0, 0 };

/*****************************************************************************
 *
 * Description:
 *    The first function to execute 
 *
 ****************************************************************************/
int
main(void)
{
  tU8 error;
  tU8 pid;

  //immediately turn off buzzer (if connected)
  IODIR0 |= 0x00000080;
  IOSET0  = 0x00000080;
  
  osInit();
  osCreateProcess(initProc, initStack, INIT_STACK_SIZE, &pid, 1, NULL, &error);
  osStartProcess(pid, &error);
  
  osStart();
  return 0;
}

/*****************************************************************************
 *
 * Description:
 *    Delay.
 *
 * Params:
 *    [in] arg - Number of miliseconds to wait. 
 *
 * Side effects:
 *    Timer #1 reprogrammed.
 *
 ****************************************************************************/
void udelay(unsigned int microseconds)
{
  T1TCR = 0x02; // Stop and reset timer
  T1PR  = 0x00; // Set prescaler to zero  // LETS THROUGH EVERY (VALUE+1) IMPULSE
  T1MR0 = (microseconds * (CORE_FREQ / 1000)) / 1000;
  T1IR  = 0xff; // Reset all interrrupt flags
  T1MCR = 0x04; // Stop timer on match
  T1TCR = 0x01; // Start timer
  
  // Wait until delay time has elapsed
  while (T1TCR & 0x01) 
  {

  }
}

/*****************************************************************************
 *
 * Description:
 *    A process entry function 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
proc1(void* arg)
{
  initKeyProc(); // Initialize joystick

  // State
  tU16 wholeNumber = 0; // Temperature, number before comma
  tU16 decimalNumber = 0; // Temperature, number after comma
  tU16 targetTemperature = 23; // When to activate alarm
  tBool isTargetReached = FALSE; // Should alarm be on
  tU8  i = 0; // Sound loop index
  tU8 keypress; // Constants from key.h

  for (;;)
  {
    // Read temperature
    printf("\nReading temperature.\n");
    tU8 data[3];
    if (1 == lm75Read(0x90, &data[0], 3))
    {
      tU16 temperature;
  
      temperature = (((tU16)data[0]<<8) + (tU16)data[1]) >> 7;
      wholeNumber = temperature / 2;
      decimalNumber = (temperature&1) * 5;
  
      printf("\rCurrent temperature is %d.%d", wholeNumber, decimalNumber);
    }
    else
    {
      printf("\nFAILED TO READ TEMPERATURE!\n");
    }

    // Read joystick
    keypress = checkKeyUpDown();
    if (keypress == 1 && targetTemperature < 60)
    {
      targetTemperature += 1;
    } 
    else if (keypress == 2 && targetTemperature > 1)
    {
      targetTemperature -= 1;
    }

    // Print temperature
    printf("\nDisplaying temperature.\n");
    char message[] = "00.0\nTarget: 00.0";

    // Current
    message[0] += wholeNumber / 10;
    message[1] += wholeNumber % 10;
    message[3] += decimalNumber;

    // Target
    message[13] += targetTemperature / 10;
    message[14] += targetTemperature % 10;

    messageOnLCD(message, FALSE);
  
    // Compare measured temperature to target
    if (wholeNumber > targetTemperature || ((wholeNumber == targetTemperature) && (decimalNumber != 0)))
    {
      isTargetReached = TRUE;
  
      printf("\nTarget temperature exceeded!\n");
    }
    else
    {
      isTargetReached = FALSE;
  
      printf("\nBelow target temperature.\n");
    }

    // Light diodes
    if (!isTargetReached) {
      if (targetTemperature - wholeNumber > 2) {
        lightRGB(0, 0, 255);
      } else {
        lightRGB(255, 255, 0);
      }
    }
  
    // Sound the alarm
    if (isTargetReached) {
      printf("\nPlaying alarm.\n");

      tS32 val;
      for (i = 0; i < sizeof(alarm); i++)
      {
        // Original method
        val = alarm[i] - 128;
        val = val * 2;
    
        if (val > 127) val = 127;
        else if (val < -127) val = -127;
    
        DACR = ((val+128) << 8) |  // Actual value to output
              (1 << 16);          // BIAS = 1, 2.5uS settling time
        
        // Or...
        // DACR = (val << 6) | 0x00010000;
    
        printf("\nPlayed a note.\n");

        // Flash diodes
        if (i % 2 == 0) {
          lightRGB(0, 0, 0);
        } else {
          lightRGB(255, 0, 0);
        }

        // Delay 125 us = 850 for 8kHz, 600 for 11 kHz
        udelay(125);
    
        // Original delay
        // for(i=0; i<850; i++)
        //   asm volatile (" nop");
      }
    }

    printf("\nPause before next measurement.\n");

    // Pause equal to alarm duration
    udelay(125 * sizeof(alarm));
  }
 }

/*****************************************************************************
 *
 * Description:
 *    A process entry function 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
proc2(void* arg)
{
	testLedMatrix();
}

/*****************************************************************************
 *
 * Description:
 *    A process entry function 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
proc3(void* arg)
{
	testLcd();
}

/*****************************************************************************
 *
 * Description:
 *    A process entry function 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
proc4(void* arg)
{
	for(;;)
	{
		testRGB();
		testMotor();
	}
}

/*****************************************************************************
 *
 * Description:
 *    A process entry function 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
proc5(void* arg)
{
	testAdc();
}

/*****************************************************************************
 *
 * Description:
 *    The entry function for the initialization process. 
 *
 * Params:
 *    [in] arg - This parameter is not used in this application. 
 *
 ****************************************************************************/
static void
initProc(void* arg)
{
  tU8 error;

  eaInit(); // Initialize consol
  i2cInit(); // Initialize I2C

  // IODIR |= 0x00000380; // If the speaker doesn't work
  // IOCLR  = 0x00000380; // try these

  // Initialize DAC: AOUT = P0.25
  PINSEL1 &= ~0x000C0000;
  PINSEL1 |=  0x00080000;

  osCreateProcess(proc1, proc1Stack, PROC1_STACK_SIZE, &pid1, 3, NULL, &error);
  osStartProcess(pid1, &error);

  osDeleteProcess();
}

/*****************************************************************************
 *
 * Description:
 *    The timer tick entry function that is called once every timer tick
 *    interrupt in the RTOS. Observe that any processing in this
 *    function must be kept as short as possible since this function
 *    execute in interrupt context.
 *
 * Params:
 *    [in] elapsedTime - The number of elapsed milliseconds since last call.
 *
 ****************************************************************************/
void
appTick(tU32 elapsedTime)
{
  msClock += elapsedTime;
}
