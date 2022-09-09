#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "akuhei2c.h"

#include <dos/dos.h>
#include <dos/rdargs.h>

#include <signal.h>

/* My custom RDArgs */
struct RDArgs *myrda;

#define TEMPLATE "A=Address/A"
#define OPT_ADDR 0
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

ULONG stol(STRPTR s) {
	ULONG r;
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

BOOL detect_pca(pca9564_state_t *sc, BOOL strict = TRUE)
{
	UBYTE a, d;

	if(clockport_read(sc, I2CSTA) != 0xF8)
	 	return FALSE;

	if(strict && (clockport_read(sc, I2CADR) != 0x00))
	 	return FALSE;

	if(strict && (clockport_read(sc, I2CDAT) != 0x00))
	 	return FALSE;

	a = clockport_read(sc, I2CADR),
	d = clockport_read(sc, I2CDAT),

	clockport_write(sc, I2CDAT, 0xCC);
	clockport_write(sc, I2CADR, 0x44);
	if((clockport_read(sc, I2CDAT) == 0xCC) && (clockport_read(sc, I2CADR) == 0x44))
	{
		/* restore */
		clockport_write(sc, I2CADR, a);
		clockport_write(sc, I2CDAT, d);
		return TRUE;
	}
	return FALSE;
}

void print_pca_state(pca9564_state_t *sc)
{
	printf("0x%08X  0x%02X 0x%02X 0x%02X 0x%02X", sc->cp,
																								clockport_read(sc, I2CSTA),
																								clockport_read(sc, I2CDAT),
																								clockport_read(sc, I2CADR),
																								clockport_read(sc, I2CCON));
};

UBYTE *cps[] = { (UBYTE *)0xD80001, (UBYTE *)0xD84001, (UBYTE *)0xD88001, (UBYTE *)0xD8C001, (UBYTE *)0xD90001 };

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

	size = 1;
	chip_addr = 0;
	reg_addr = 0;

#ifdef DEBUG
	sc.in_isr = FALSE;
	sc.isr_called = 0;
#endif /* DEBUG */
	sc.cp = (UBYTE *)CLOCKPORT_BASE;
	sc.cur_op = OP_NOP;

	/* sc.sig_intr = -1;
	if ((sc.sig_intr = AllocSignal(-1)) == -1) {
		printf("Couldn't allocate signal\n");
		return 1;
	}
	sc.sigmask_intr = 1L << sc.sig_intr;

	sc.MainTask = FindTask(NULL);*/

	printf("ClockBase    STA  DAT  ADR  CON detected?\n");

	if (myrda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) {
	/* parse my command line */
		if (ReadArgs(TEMPLATE, result, myrda) && (strlen((char *)result[0]) > 0)) {
			if(result[OPT_ADDR]) {
				s = strlen((STRPTR)result[OPT_ADDR]);
				if((s == 6) ||
				((strncmp((STRPTR)result[OPT_ADDR], "0x", 2) == 0) && ((s == 10) || (s == 8)))) {
					sc.cp = (UBYTE *)stol((STRPTR)result[OPT_ADDR]);
					print_pca_state(&sc);
					if(detect_pca(&sc)) {
						printf(" yes\n");
					} else {
						printf(" no\n");
					}
				}
			}
			FreeArgs(myrda);
			FreeDosObject(DOS_RDARGS, myrda);
		} else {
			for(k = 0; k < sizeof(cps)/sizeof(UBYTE*); ++k) {
				sc.cp = cps[k];
				print_pca_state(&sc);
				if(detect_pca(&sc)) {
					printf(" yes\n");
					break;
				} else {
					printf(" no\n");
				}
			}
		}
	}

	return 0;
}
