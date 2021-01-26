/*
 *  Lib(X)SVF  -  A library for implementing SVF and XSVF JTAG players
 *
 *  Copyright (C) 2009  RIEGL Research ForschungsGmbH
 *  Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>
 *  Copyright 2017 Google Inc <philpearson@google.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <Arduino.h>
extern "C" {
#include "libxsvf.h"
}

// Uncomment this to dump out a lot of debug info
// #define NOISY

// Uncomment this to silence pretty much everything
#define QUIET

// Uncomment this to skip delays entirely
#define delayMicroseconds(us) ((void)(us))

// Uncomment this to use faster IO for TCK (speeds up RUNTEST commands)
// -- this only works for an ATMEGA32U4 with TCK on pin 20 (PF5)
#ifdef __AVR__
#define FAST_TCK_0_1() do { \
	PORTF &= ~(1<<5); \
	__asm__("nop\n\t""nop\n\t""nop\n\t""nop\n\t" \
		"nop\n\t""nop\n\t""nop\n\t""nop\n\t"); \
	PORTF |= (1<<5); \
	__asm__("nop\n\t""nop\n\t""nop\n\t""nop\n\t" \
		"nop\n\t""nop\n\t""nop\n\t""nop\n\t"); \
} while (0)
#endif

struct arduino_xsvf_user_data {
	unsigned long frequency;
	int tms_pin;
	int tdi_pin;
	int tdo_pin;
	int tck_pin;
	int trst_pin;
};

static int h_setup(struct libxsvf_host *h)
{
	struct arduino_xsvf_user_data *u = (struct arduino_xsvf_user_data *)h->user_data;
	pinMode(u->tms_pin, OUTPUT);
	pinMode(u->tdi_pin, OUTPUT);
	pinMode(u->tdo_pin, INPUT);
	pinMode(u->tck_pin, OUTPUT);
	if (u->trst_pin >= 0) pinMode(u->trst_pin, OUTPUT);
	return 0;
}

static int h_shutdown(struct libxsvf_host *h)
{
	Serial.println("h_shutdown");
	return 0;
}

static void h_udelay(struct libxsvf_host *h, long usecs, int tms, long num_tck)
{
	struct arduino_xsvf_user_data *u = (struct arduino_xsvf_user_data *)h->user_data;
#ifdef NOISY
	Serial.print("Delay ");
	Serial.print(usecs);
	Serial.print(" us and ");
	Serial.print(num_tck);
	Serial.println(" tcks");
	Serial.print("normal delay is ");
	Serial.println(1000000L / u->frequency);
#endif

	delayMicroseconds(usecs);
	digitalWrite(u->tms_pin, tms ? HIGH : LOW);
	for (long i = 0; i < num_tck; ++i) {
#ifdef FAST_TCK_0_1
		FAST_TCK_0_1();
#else
		digitalWrite(u->tck_pin, LOW);
		delayMicroseconds(1000000L / u->frequency);
		digitalWrite(u->tck_pin, HIGH);
		delayMicroseconds(1000000L / u->frequency);
#endif
	}
#ifdef NOISY
	Serial.println("udelay done");
#endif
}

static int h_getbyte(struct libxsvf_host *h)
{
	unsigned long now = millis();
	if (Serial.available() == 0) {
		Serial.println("*#"); // ask for more bytes
	}
	while (!Serial.available()) {
		if (!Serial) return -1;  // Cancel on disconnect
		if (millis() - now > 1000) {
			Serial.println("getbyte timeout, returning EOF");
			return -1;
		}
	}
	int c = Serial.read();
	if (c == 4) return -1;  // Terminate file with ^D
	return c;
}

static int h_pulse_tck(struct libxsvf_host *h, int tms, int tdi, int tdo, int rmask, int sync)
{
	struct arduino_xsvf_user_data *u = (struct arduino_xsvf_user_data *)h->user_data;

	digitalWrite(u->tms_pin, tms ? HIGH : LOW);
	digitalWrite(u->tdi_pin, tdi ? HIGH : LOW);

	digitalWrite(u->tck_pin, LOW);
	delayMicroseconds(1000000L / u->frequency);
	digitalWrite(u->tck_pin, HIGH);
	delayMicroseconds(1000000L / u->frequency);
	int line_tdo = digitalRead(u->tdo_pin) == HIGH ? 1 : 0;

#ifdef NOISY
	if (tdo >= 0 && line_tdo != tdo) {
		Serial.print("Expected tdo=");
		Serial.print(tdo);
		Serial.print(" but got ");
		Serial.println(line_tdo);
	} else if (tdo == -1) {
		Serial.print("Got tdo=");
		Serial.println(line_tdo);
	} else {
		Serial.print("Got tdo=");
		Serial.print(line_tdo);
		Serial.println(" as expected");
	}
#endif
	return (tdo < 0 || line_tdo == tdo) ? line_tdo : -1;
}

static void h_set_trst(struct libxsvf_host *h, int v)
{
	struct arduino_xsvf_user_data *u = (struct arduino_xsvf_user_data *)h->user_data;
	if (u->trst_pin >= 0) {
		digitalWrite(u->trst_pin, v ? HIGH : LOW);
	}
}

static int h_set_frequency(struct libxsvf_host *h, unsigned long v)
{
	struct arduino_xsvf_user_data *u = (struct arduino_xsvf_user_data *)h->user_data;
	u->frequency = v;
	return 0;
}

static void h_report_tapstate(struct libxsvf_host *h)
{
	// Serial.print("[");
	// Serial.print(libxsvf_state2str(h->tap_state));
	// Serial.println("]");
}

static void h_report_device(struct libxsvf_host *h, unsigned long idcode)
{
	Serial.print("idcode=0x");
	Serial.print(idcode, HEX);
	Serial.print(", revision=0x");
	Serial.print((idcode >> 28) & 0xf, HEX);
	Serial.print(", part=0x");
	Serial.print((idcode >> 12) & 0xffff, HEX);
	Serial.print(", manufacturer=0x");
	Serial.println((idcode >> 1) & 0x7ff, HEX);
}

static void h_report_status(struct libxsvf_host *h, const char *message)
{
#ifndef QUIET
	Serial.print("[STATUS] ");
	Serial.println(message);
#endif
}

static void h_report_error(struct libxsvf_host *h, const char *file, int line, const char *message)
{
	Serial.print("[");
	Serial.print(file);
	Serial.print(":");
	Serial.print(line);
	Serial.print("] ");
	Serial.println(message);
}

static void *h_realloc(struct libxsvf_host *h, void *ptr, int size, enum libxsvf_mem which)
{
	// Serial.println("realloc");
	static unsigned char buf_svf_commandbuf[200];
	static unsigned char buf_svf_sdr_tdi_data[16];
	static unsigned char buf_svf_sdr_tdi_mask[16];
	static unsigned char buf_svf_sdr_tdo_data[16];
	static unsigned char buf_svf_sdr_tdo_mask[16];
	static unsigned char buf_svf_sir_tdi_data[1];
	static unsigned char buf_svf_sir_tdi_mask[1];
	static unsigned char buf_svf_sir_tdo_data[1];
	static unsigned char buf_svf_sir_tdo_mask[1];
	static unsigned char *buflist[15] = {
		(unsigned char *)0, (unsigned char *)0, (unsigned char *)0, (unsigned char *)0,
		(unsigned char *)0, buf_svf_commandbuf, buf_svf_sdr_tdi_data, buf_svf_sdr_tdi_mask,
		buf_svf_sdr_tdo_data, buf_svf_sdr_tdo_mask, (unsigned char *)0, buf_svf_sir_tdi_data,
		buf_svf_sir_tdi_mask, buf_svf_sir_tdo_data, buf_svf_sir_tdo_mask };
	static int sizelist[15] = { 0, 0, 0, 0, 0, sizeof(buf_svf_commandbuf), sizeof(buf_svf_sdr_tdi_data), sizeof(buf_svf_sdr_tdi_mask), sizeof(buf_svf_sdr_tdo_data), sizeof(buf_svf_sdr_tdo_mask), 0, sizeof(buf_svf_sir_tdi_data), sizeof(buf_svf_sir_tdi_mask), sizeof(buf_svf_sir_tdo_data), sizeof(buf_svf_sir_tdo_mask) };
	if (which < 15 && size > sizelist[which]) {
		Serial.print("Error attempting to allocate ");
		Serial.print(size);
		Serial.print(" bytes for libxsvf realloc entry ");
		Serial.println(which);
	}
	return which < 15 && size <= sizelist[which] ? buflist[which] : (void*)0;
}

void arduino_play_svf(int tms_pin, int tdi_pin, int tdo_pin, int tck_pin, int trst_pin) {
	struct arduino_xsvf_user_data u;
	memset(&u, 0, sizeof(u));
	u.frequency = 100000;
	u.tms_pin = tms_pin;
	u.tdi_pin = tdi_pin;
	u.tdo_pin = tdo_pin;
	u.tck_pin = tck_pin;
	u.trst_pin = trst_pin;

	struct libxsvf_host h;
	memset(&h, 0, sizeof(h));
	h.udelay = h_udelay;
	h.setup = h_setup;
	h.shutdown = h_shutdown;
	h.getbyte = h_getbyte;
	h.pulse_tck = h_pulse_tck;
	h.set_trst = h_set_trst;
	h.set_frequency = h_set_frequency;
	h.report_tapstate = h_report_tapstate;
	h.report_device = h_report_device;
	h.report_status = h_report_status;
	h.report_error = h_report_error;
	h.realloc = h_realloc;
	h.user_data = (void *)&u;

	if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
		Serial.println("Failed to scan chain");
	} else {
		Serial.println("JTAG scan done");
	}

	if (libxsvf_play(&h, LIBXSVF_MODE_SVF) < 0) {
		Serial.println("Error while playing SVF from Serial");
	} else {
		Serial.println("SVF playback done");
	}
}
