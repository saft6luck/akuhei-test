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

/*#define TEMPLATE "A=Address/A,R=Register,V=Value,W=WordMode/S"*/
#define TEMPLATE "A=Address/A,R=Register,V=Value"
#define OPT_ADDR 0
#define OPT_REGISTER  1
#define OPT_VALUE  2
#define OPT_WRITEMODE  3
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

ULONG stoi(STRPTR s) {
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
	ULONG reg_value;
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

	printf("Specified parameters (%d):\n", argc);

	for (argNo=0; argNo < argc; ++argNo) {
		printf("   argument #%d = >%s<\n", argNo, argv[argNo]);
	}

	/* Need to ask DOS for a RDArgs structure */
	if (myrda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) {
	/* parse my command line */
		if (ReadArgs(TEMPLATE, result, myrda) && (strlen((char *)result[0]) > 0)) {
			if(result[OPT_ADDR]) {
				s = strlen((STRPTR)result[OPT_ADDR]);
				if((s == 2) || (strncmp((STRPTR)result[OPT_ADDR], "0x", 2) == 0) && (s == 4)) {
					chip_addr = stoi((STRPTR)result[OPT_ADDR]);
					printf("Chip address Specified : >%s<, len=%d -> 0x%02X\n", (STRPTR)result[OPT_ADDR], strlen((STRPTR)result[OPT_ADDR]), chip_addr);
				}
				if(result[OPT_REGISTER]) {
					s = strlen((STRPTR)result[OPT_REGISTER]);
					if(strncmp((STRPTR)result[OPT_REGISTER], "0x", 2) == 0)
						s -= 2;
					if(s%2)
						printf("invalid length of argument value (%u) >%s<.\n", s--, (STRPTR)result[OPT_REGISTER]);
					size = s / 2;
					reg_addr = stoi((STRPTR)result[OPT_REGISTER]);
					printf("Register address Specified : >%s<, len=%u -> %u\n", (STRPTR)result[OPT_REGISTER], s, reg_addr);
				}
				if(result[OPT_VALUE]) {
					s = strlen((STRPTR)result[OPT_VALUE]);
					if(strncmp((STRPTR)result[OPT_VALUE], "0x", 2) == 0)
						s -= 2;
					if(s%2)
						printf("invalid length of argument value (%u) >%s<.\n", s--, (STRPTR)result[OPT_VALUE]);
					size += s / 2;
					reg_value = stoi((STRPTR)result[OPT_VALUE]);
					printf("Register value Specified : >%s<, len=%u -> 0x%lX\n", (STRPTR)result[OPT_VALUE], s, reg_value);
				}
			}
			FreeArgs(myrda);
		} else {
			printf("ReadArgs returned NULL\n");
		}
		PutStr("Usage: " TEMPLATE "\n");
		FreeDosObject(DOS_RDARGS, myrda);
	} else {
		PutStr("Usage: " TEMPLATE "\n");
	}

	if((chip_addr < 0x03) || (chip_addr > 0x77)) {
		PutStr("Chip address out side of range.\n");
		return 1;
	}

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

	/* init the host controller */
	/*ctrl = I2CCON_CR_59KHZ | I2CCON_ENSIO;*/
	ctrl = I2CCON_CR_330KHZ | I2CCON_ENSIO;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);

	buf[0] = reg_addr; /*0xAC;*/ /* configuration register */

	for (argNo=size-1; argNo > 0; --argNo) {
		buf[argNo] = (UBYTE)(0xFF & reg_value);
		reg_value >>= 8;
	}

	printf("transmitting (%u bytes):", size);
	for (argNo=0; argNo < size; ++argNo)
		printf(" 0x%02x", buf[argNo]);
	printf("\n");

	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
	}

	pca9564_write(&sc, chip_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {
		printf("transmitted (%u): ", size);
		for (argNo=0; argNo < size; ++argNo) {
			printf(" 0x%02x", buf[argNo]);
			/*printf("read result: 0x%02X, %c0x%02X = %d.%02d%cC\n", buf[0], s, buf[1], buf[0], temperat, 0xb0);*/
			/*printf("LM75 at addr 0x%02x: %c%d.%02d%cC\n", i2c_sensor_addr, s, buf[0], temperat, 0xb0);*/
		}
		printf("\n");
	} else {
		printf("received error\n");
	}

	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
		Delay(50);
	}

	ctrl = 0;
	clockport_write(&sc, I2CCON, ctrl);

	pca9564_dump_state(&sc);

	FreeMem(buf, size);

	RemIntServer(INTB_EXTER, int6);
	FreeMem(int6, sizeof(struct Interrupt));
	FreeSignal(sc.sig_intr);

#ifdef DEBUG
	printf("ISR was called %d times\n", sc.isr_called);
#endif /* DEBUG */

	return 0;
}
