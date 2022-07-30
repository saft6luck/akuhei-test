#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*#include <stdint.h>*/

#include "akuhei2c.h"

#include <dos/dos.h>
#include <dos/rdargs.h>

#include <signal.h>

/* My custom RDArgs */
struct RDArgs *myrda;

#define TEMPLATE "A=Address/A,R=Register,W=WordMode/S"
#define OPT_ADDR 0
#define OPT_REGISTER  1
#define OPT_READMODE  2
LONG result[3];

UBYTE atoh(char c) {
	UBYTE r;
	if ((c <='9') && (c >= '0')) {
		r = c - '0';
	} else {
		r = 10 + c;
		if ((c <= 'F') && (c >= 'A'))
			r -= 'A';
		else
			r -= 'a';
	}
	return r;
}

UBYTE stoi(STRPTR s) {
	UBYTE r;
	while(*s) {
		r <<= 4;
		if((*s == 'x') || (*s == 'X'))
			r = 0;
		else
			r += atoh((char)*s);
		++s;
	}
	return r;
}

typedef enum {
	READ_BYTE,
	READ_WORD
} read_mode_t;

int main(int argc, char **argv)
{
	pca9564_state_t sc;
	struct Interrupt *int6;

	UBYTE ctrl;
	UBYTE *buf;
	UBYTE size;
	UBYTE p;
	UBYTE s;
	int argNo;
	unsigned short temperat;

	UBYTE chip_addr, reg_addr;
	LONG *strp;
	STRPTR sptr;
	UBYTE **arguments;
	UBYTE k;

	/*struct sigaction act;*/

	read_mode_t read_mode;

	size = 1;
	chip_addr = 0;
	reg_addr = 0;
	read_mode = READ_BYTE;

#ifdef DEBUG
	sc.in_isr = FALSE;
	sc.isr_called = 0;
#endif /* DEBUG */
	sc.cp = CLOCKPORT_BASE;
	sc.cur_op = OP_NOP;

	sc.sig_intr = -1;
	if ((sc.sig_intr = AllocSignal(-1)) == -1) {
		printf("Couldn't allocate signal\n");
		return 1;
	}
	sc.sigmask_intr = 1L << sc.sig_intr;

	sc.MainTask = FindTask(NULL);

	if (int6 = AllocMem(sizeof(struct Interrupt), MEMF_PUBLIC|MEMF_CLEAR)) {
		int6->is_Node.ln_Type = NT_INTERRUPT;
		int6->is_Node.ln_Pri = -60;
		int6->is_Node.ln_Name = "PCA9564";
		int6->is_Data = (APTR)&sc;
		int6->is_Code = pca9564_isr;

		AddIntServer(INTB_EXTER, int6);
	} else {
		printf("Can't allocate memory for interrupt node\n");
		FreeSignal(sc.sig_intr);
		return 1;
	}

	if (!(buf = AllocMem(size, MEMF_PUBLIC|MEMF_CLEAR))) {
		printf("Not enough memory to allocate the buffer\n");
		return 1;
		/* XXX: clean up */
	}

	if((clockport_read(&sc, I2CSTA) != 0xF8)
	|| (clockport_read(&sc, I2CDAT) != 0x00)
	|| (clockport_read(&sc, I2CADR) != 0x00)) {
		s = clockport_read(&sc, I2CADR);
		clockport_write(&sc, I2CADR, ~s);
		if(!(clockport_read(&sc, I2CADR) ^ s)) {
			PutStr("PCA9564 not detected.\n");
		}
		clockport_write(&sc, I2CADR, s);
	}
	PutStr("I2C in state: \n");
	pca9564_dump_state(&sc);

	/* init the host controller */
	/*ctrl = I2CCON_CR_59KHZ | I2CCON_ENSIO;*/
	ctrl = I2CCON_CR_330KHZ | I2CCON_ENSIO;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);

	s = clockport_read(&sc, I2CSTA);

	ctrl = 0;
	clockport_write(&sc, I2CCON, ctrl);
	s = clockport_read(&sc, I2CSTA);
	if(s != I2CSTA_IDLE) {
		Delay(50);
		clockport_write(&sc, I2CCON, ctrl);
	} else {
		Delay(5);
	}

	FreeMem(buf, size);

	RemIntServer(INTB_EXTER, int6);
	FreeMem(int6, sizeof(struct Interrupt));
	FreeSignal(sc.sig_intr);

#ifdef DEBUG
	printf("ISR was called %d times\n", sc.isr_called);
#endif /* DEBUG */

	return 0;
}
