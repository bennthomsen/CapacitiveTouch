//******************************************************************************
//  MSP430G2553 Demo - Capacitive Touch, Pin Oscillator Method, 1 button
//
//  Description: Basic 1-button input using the built-in pin oscillation feature
//  on GPIO input structure. PinOsc signal feed into TA0CLK. WDT interval is used
//  to gate the measurements. Difference in measurements indicate button touch.
//  
//  ACLK = VLO = 12kHz, MCLK = SMCLK = 1MHz DCO
//
//               MSP430G2xx3
//             -----------------
//         /|\|              XIN|-
//          | |                 | 
//          --|RST          XOUT|-
//            |                 |
//            |             P2.0|<--Capacitive Touch Input 1
//            |                 |
//  LED 2  <--|P1.6             |
//            |                 |
//  LED 1  <--|P1.0             |
//            |                 |
//            |                 |
//
//  Modified for Energia and GCC
//  Benn Thomsen
//  March 2016
//
//  Based on source from
//  Brandon Elliott/D. Dang
//  Texas Instruments Inc.
//  November 2010
//******************************************************************************

#include "UART.h"
#include "print.h"
#include <inttypes.h>

#define TOUCHPIN BIT0   //P2.0

/* Define User Configuration values */
/* Defines WDT SMCLK interval for sensor measurements*/
#define WDT_meas_setting (DIV_SMCLK_512)
/* Defines WDT ACLK interval for delay between measurement cycles*/
#define WDT_delay_setting (DIV_ACLK_512)

/* Sensor settings*/
#define KEY_LVL     220                     // Defines threshold for a key press

/* Definitions for use with the WDT settings*/
#define DIV_ACLK_32768  (WDT_ADLY_1000)     // ACLK/32768
#define DIV_ACLK_8192   (WDT_ADLY_250)      // ACLK/8192 
#define DIV_ACLK_512    (WDT_ADLY_16)       // ACLK/512 
#define DIV_ACLK_64     (WDT_ADLY_1_9)      // ACLK/64 
#define DIV_SMCLK_32768 (WDT_MDLY_32)       // SMCLK/32768
#define DIV_SMCLK_8192  (WDT_MDLY_8)        // SMCLK/8192 
#define DIV_SMCLK_512   (WDT_MDLY_0_5)      // SMCLK/512 
#define DIV_SMCLK_64    (WDT_MDLY_0_064)    // SMCLK/64 

#define TOUCH_BUTTON 0
#define TOUCH_PROXIMITY 1

// Global variables for sensing
unsigned int base_cnt, meas_cnt;
int delta_cnt, j;
char key_pressed;
int cycles = 0;
/* System Routines*/
void measure_count(uint8_t pin);                   // Measures each capacitive sensor

int main(void) {
  WDTCTL = WDTPW + WDTHOLD;    // Stop WDT
  DCOCTL = 0;                  // Select lowest DCOx and MODx settings
  BCSCTL1 = CALBC1_1MHZ;       // Set DCO to 1MHz
  DCOCTL = CALDCO_1MHZ;
  BCSCTL3 |= LFXT1S_2;         // LFXT1 = VLO 12kHz

  IE1 |= WDTIE;                // enable WDT interrupt
  
  UARTConfigure();             // Initialise UART for serial comms
  __bis_SR_register(GIE);      // Enable interrupts
  
  get_base_count(TOUCHPIN);            // Get baseline 

  while(1) {
    key_pressed = 0;                      // Assume no keys are pressed
    measure_count(TOUCHPIN);              // measure pin oscillator
    delta_cnt = base_cnt - meas_cnt;      // Calculate delta: c_change
    printformat("Baseline: %i Raw count: %i Difference: %i\r\n",base_cnt,meas_cnt,delta_cnt);

    // Handle baseline measurement for a baseline decrease
    if (delta_cnt < 0) {                        // If delta negative then raw value is larger than baseline
        base_cnt = (base_cnt + meas_cnt) >> 1;  // Update baseline average
    }
    if (delta_cnt > KEY_LVL) {                  // Determine if capacitance change is greater than the threshold 
        key_pressed = 1;                        // key pressed
        UARTPrintln("Presense detected");
    } else key_pressed = 0;

    /* Delay to next sample, sample more slowly if no keys are pressed*/
    if (key_pressed) {
        BCSCTL1 = (BCSCTL1 & 0x0CF) + DIVA_0;    // Set WDT Clock = ACLK/1
        cycles = 20;
    } else {
        if (cycles-- == 0){  
            BCSCTL1 = (BCSCTL1 & 0x0CF) + DIVA_3; // Set WDT Clock = ACLK/8
            cycles = 0;
        }
    } 
    WDTCTL = WDT_delay_setting;             // Start Watchdog timer for delay. WDT, ACLK, interval timer
    __bis_SR_register(LPM3_bits);           // Put CPU in low power mode 3 to wait for WDT interupt
    }
}

void get_base_count(uint8_t pin) {
// Take 16 measurements and obtain an average.
    unsigned int i;
    base_cnt = 0;
    for (i = 16; i > 0; i--) {
        measure_count(pin);
        base_cnt = meas_cnt + base_cnt;
    }
    base_cnt = base_cnt >> 4;    // Divide by 16
}

void measure_count(uint8_t pin) {

    TA0CTL = TASSEL_3 + MC_2;           // INCLK from Pin oscillator, continous count mode to 0xFFFF
    TA0CCTL1 = CM_3 + CCIS_2 + CAP;     // Capture on Rising and Falling Edge,Capture input GND,Capture mode

    /*Configure Ports for relaxation oscillator*/
    /*The P2SEL2 register allows Timer_A to receive it's clock from a GPIO*/
    /*See the Application Information section of the device datasheet for info*/
    P2DIR &= ~ pin;                    // P2.0 is the input used here
    P2SEL &= ~ pin;
    P2SEL2 |= pin;

    /*Setup Gate Timer*/
    WDTCTL = WDT_meas_setting;              // Start WDT, Clock Source: ACLK, Interval timer
    TA0CTL |= TACLR;                        // Reset Timer_A (TAR) to zero
    __bis_SR_register(LPM0_bits + GIE);     // Wait for WDT interrupt
    TA0CCTL1 ^= CCIS0;                      // Toggle the counter capture input to capture count using TACCR1
    meas_cnt = TACCR1;                      // Save result
    WDTCTL = WDTPW + WDTHOLD;               // Stop watchdog timer
    P2SEL2 &= ~ pin;                        // Disable Pin Oscillator function
}

#pragma vector=WDT_VECTOR
__interrupt void watchdog_timer(void)
{
    __bic_SR_register_on_exit(LPM3_bits);   // Exit LPM3 on reti
}

