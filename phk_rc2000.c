/*-
 * Copyright (c) 2005 Poul-Henning Kamp
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: phk_rc2000.c,v 1.11 2010/03/16 18:15:10 phk Exp $
 *
 */

#ifdef FLEXELINT
typedef unsigned char __sfr;
#endif

#define SERIAL	0

#include "pic18fregs.h"

#include <stdint.h>


#ifndef CTASSERT                /* Allow lint to override */
#define CTASSERT(x)             _CTASSERT(x, __LINE__)
#define _CTASSERT(x, y)         __CTASSERT(x, y)
#define __CTASSERT(x, y)        typedef char __assert ## y[(x) ? 1 : -1]
#endif

#pragma stack 0x200 0x100

/* Enable the watchdog at 4msec * 64 = .256 sec */
code unsigned char at(__CONFIG2H) config2h = (0x7 << 1) | 1;

/* HSPLL */
code unsigned char at(__CONFIG2L) config2l = 0
	| (1 << 7)	// IESO two speed startup
	| (1 << 6)	// FCMEM
	| (1 << 4)	// LPT1OSC
	| (1 << 3)	// T1DIG
	| (3 << 0)	// ECPLL CLKO@RA6
	;

/* No code protects.  No CPU system clock divde */
code unsigned char at(__CONFIG1H) config1h = 0
	| (0xf0)	// unimplemented
	| (1 << 2)	// CP0
	| (3 << 0)	// CPDIV = /1
	;

/* No debugger, no extins, reset on stack, WDT enable, PLLDIV=? */
code unsigned char at(__CONFIG1L) config1l = 0
	| (1 << 7)	// #DEBUG
	| (0 << 6)	// XINST
	| (1 << 5)	// STVREN
	| (6 << 1)	// PLLDIV = 8 MHz
	| (1 << 0)	// WDTEN
	;

#define MHZ	48		// Just so we remember

/* Serial port defines -----------------------------------------------*/

#if SERIAL

#define SERIALPORTS (1 << 2)

#include "serial.c"

/*********************************************************************/
static uint8_t pcbuf[256];
static volatile uint8_t pcbuf_r;
static volatile uint8_t pcbuf_w;
static volatile uint8_t pcoflo;

static void
pc_poll(void)
{
	uint8_t c;

	// INTCON = 0x00;
	if (!SERIAL_TXRDY(2)) {
		
	} else if (pcbuf_r == pcbuf_w && pcoflo) {
		SERIAL_TX(2, '!');
		pcoflo = 0;
		PIE3bits.TX2IE = 1;
	} else if (pcbuf_r != pcbuf_w) {
		c = pcbuf[pcbuf_r++];
		SERIAL_TX(2, c);
		PIE3bits.TX2IE = 1;
	} else {
		PIE3bits.TX2IE = 0;
	}
	// INTCON = 0xc0;
}

void
putchar(char c) __wparam
{
	volatile uint8_t i;

	// INTCON = 0x00;
	i = pcbuf_w;
	if (i + 1 == pcbuf_r) 
		pcoflo = 1;
	if (!pcoflo) {
		pcbuf[i] = c;
		pcbuf_w = i + 1;
	}
	if (SERIAL_TXRDY(2)) 
		pc_poll();
	// INTCON = 0xc0;
}

#endif

#include "usb.c"

/*********************************************************************/
static uint8_t txBuffer[64];
static uint8_t txbp;
static uint8_t rxBuffer[64];
static uint8_t rxbp, rxbe;
static uint8_t loop;

static uint8_t hmode;

static uint16_t rate;

static const uint8_t hex[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7',
	'8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

/* XXX: move buff er insertion after strobe timing ? */
static void
dochar(void)
{
	uint8_t c, u;

	if (txbp + 1 + hmode * 3 > sizeof txBuffer) 
		return;

	if (!PORTCbits.RC7)
		return;

	if ((CDC_modem & 3) != 3)		/* DTR + RTS */
		return;

	c = PORTB;
	/* XXX: validation read, to check PORTB bits are stable ? */
	PORTCbits.RC6 = 0;
	for (u = 0; u < 10; u++)
		if(!PORTCbits.RC7)
			break;
	if (hmode) {
		txBuffer[txbp++] = hex[((c & 0xf0) >> 4)];
		txBuffer[txbp++] = hex[(c & 0xf)];
		txBuffer[txbp++] = '\r';
		txBuffer[txbp++] = '\n';
	} else {
		txBuffer[txbp++] = c;
	}
	/* XXX: calibrate width of strobe pulse */
	for (u = 0; u < 30; u++)		/* 41.6 miuroseconds */
		;
	PORTCbits.RC6 = 1;
}

static const char usage[] =
	"\r\n\n"
	"RC-2000 USB adapter\r\n"
	"===================\r\n"
	"0:\tSingle Step\r\n"
	"1-9:\tSet Speed\r\n"
	"+:\tFaster\r\n"
	"-:\tSlower\r\n"
	"b:\tBinary mode\r\n"
	"h:\tHex mode\r\n"
	"?:\tThis help\r\n"
	"\n"
	"2010-02-20 Poul-Henning Kamp\r\n"
	"\n";

static void
Send(void)
{
	uint8_t j;

	j = InPipe(1, txBuffer, txbp);
	if (j == 0) {
	} else if (j == txbp) {
		txbp = 0;
	} else {
		printf("Sent %u/%u\n\r", (uint16_t)j, (uint16_t)txbp);
		txbp = 0;
	}
}

// Regardless of what the USB is up to, we check the USART to see
// if there's something we should be doing.
static void
USBEcho(void)
{
	uint8_t j;
	uint16_t x;

#if SERIAL
	uint8_t rxByte;

	if (serial_rxoerr(2)) {
		serial_init(2);
	}
	if (serial_rxrdy(2)) {
		rxByte = serial_rx(2);
		if (txbp < sizeof txBuffer)
			txBuffer[txbp++] = rxByte;
	}
#endif
	if ((deviceState < CONFIGURED) || (UCONbits.SUSPND == 1)) 
		return;

	if (!(CDC_modem & 1)) {		/* DTR */
		txbp = 0;
		rate = 0;
	}

	x = TMR0L; 
	x |= (TMR0H << 8);
	if (rate > 0 && x > rate) {
		TMR0H = 0;
		TMR0L = 0;
		dochar();
	}
	loop++;
	if (txbp == 64) 
		Send();

	if (loop)
		return;

	if (txbp > 0) 
		Send();

	if (rxbp < rxbe) {
		j = rxBuffer[rxbp];
		printf("%c", j);
		// if (txbp < sizeof txBuffer) txBuffer[txbp++] = j;
		switch (j) {
		case 'b':
			hmode = 0;
			break;
		case 'h':
			hmode = 1;
			break;
		case '0':
			rate = 0;
			dochar();
			break;
#define T0HZ	750000UL
		case '1': rate = 15000; break;	// 50 cps
		case '2': rate =  7500; break;	// 100 cps
		case '3': rate =  3750; break;	// 200 cps
		case '4': rate =  1500; break;	// 380 cps
		case '5': rate =   750; break;	// 718 cps
		case '6': rate =   600; break;	// 1034 cps
		case '7': rate =   400; break;	// 1411 cps
		case '8': rate =   300; break;	// 1780 cps
		case '9': rate =    16; break;	// 2479 cps
		case '-':
			x = rate + (rate >> 4);
			if (x > rate)
				rate = x;
			else
				rate = 0;
			break;
		case '+':
			if (rate == 0)
				rate = 65500U;
			else {
				x = rate - (rate >> 4);
				if (x < rate)
					rate = x;
			}
			break;
		case '?':
			for (j = 0; j < sizeof usage; j++) {
				txBuffer[txbp++] = usage[j];
				while (txbp == sizeof txBuffer)
					Send();
			}
			break;
		default:
			break;
		}
		printf("Rate = %u\n\r", rate);
		rxbp++;
	} else {
		rxbe = OutPipe(1, rxBuffer, sizeof rxBuffer);
		rxbp = 0;
	}
}

/*********************************************************************/

void
intr_h() interrupt 1
{
	uint8_t u;

	u = PIR2;
	PORTBbits.RB4 = 1;
	if (u & 0x10)
		USB_intr();
	PORTBbits.RB4 = 0;
	PIR2 = 0;
#if SERIAL
	u = PIR3;
	if (u & 0x10)
		pc_poll();
	PIR3 = 0;
#endif
}

void
intr_l() interrupt 2
{

}

/*********************************************************************/

void
main(void) wparam
{
	uint16_t u;

	TRISCbits.TRISC6 = 0;
#if SERIAL
	SERIAL_INIT(2);
	SERIAL_BAUD(2, 103);		// 48MHz / (4 * (115200 + 1) = 104
	TXSTA2bits.BRGH = 1;
	BAUDCON2bits.BRG16 = 1;

	// stdin = STREAM_USART;
	// stdout = STREAM_USART;
	TRISAbits.TRISA0 = 0;
	RPOR0 = 5;			// RP0 = RA0 = pin2 = TX
	RPINR16 = 1; 			// RP1 = RA1 = pin3 = RX

#endif
	stdin = STREAM_USER;
	stdout = STREAM_USER;


	OSCTUNEbits.PLLEN = 1;
	/* Wait for PLL to lock */
	for (u = 0; u < 0x8000; u++)
		;


	// Set all I/O pins to digital
	ANCON0 |= 0x00;
	ANCON1 |= 0x1F;

	/*
	 * T0 Freq = 48MHz / (4 * 16) = 750 kHz
	 */
	T0CON = 0
	    | (1 << 7)		// Enable
	    | (3 << 0)		// 1:16 Prescaler
	    ;

	// Initialize USB
	UCFG = 0x14; // Enable pullup resistors; full speed mode
	deviceState = DETACHED;
	remoteWakeup = 0x00;
	currentConfiguration = 0x00;

	INTCON2bits.RBPU = 0;		// Weak pull-up PORTB

	/* Setup Interrupts */
	RCONbits.IPEN = 1;
	INTCON = 0xc0;
	PIE2bits.USBIE = 1;


	rate = 0;
	txbp = 0;
	rxbp = 0;
	rxbe = 0;
	hmode = 1;

	while(1) {
		ClrWdt();
		// Ensure USB module is available
		EnableUSBModule();
		USBEcho();
	}
}

