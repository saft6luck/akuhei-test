#include <stdio.h>
#include <stdlib.h>
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

	printf("Specified parameters (%d):\n", argc);

	for (argNo=0; argNo < argc; ++argNo) {
		printf("   argument #%d = >%s<\n", argNo, argv[argNo]);
	}

	/* Need to ask DOS for a RDArgs structure */
	if (myrda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) {
	/* parse my command line */
		if (ReadArgs(TEMPLATE, result, myrda) && (strlen(result[0]) > 0)) {
			if(result[OPT_ADDR]) {
				s = strlen((STRPTR)result[OPT_ADDR]);
				if((s == 2) || (strncmp((STRPTR)result[OPT_ADDR], "0x", 2) == 0) && (s == 4)) {
					chip_addr = stoi((STRPTR)result[OPT_ADDR]);
					printf("Chip address Specified : >%s<, len=%d -> 0x%02X\n", (STRPTR)result[OPT_ADDR], strlen((STRPTR)result[OPT_ADDR]), chip_addr);
					if(result[OPT_REGISTER]) {
						switch(strlen((STRPTR)result[OPT_REGISTER])) {
							case 2:
							case 4:
								reg_addr = stoi((STRPTR)result[OPT_REGISTER]);
								printf("Register address Specified : >%s< -> 0x%02X\n", (STRPTR)result[OPT_REGISTER], reg_addr);
								break;
							default:
								break;
						}
					}
					if(result[OPT_READMODE]) {
						size = 2;
					}
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

	buf[0] = 0xAC; /* configuration register */
	buf[1] = 0x8C; /* high resolution */
	/*pca9564_write(&sc, i2c_sensor_addr, 2, &buf);*/

	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
	}

	/* read 2 bytes from 0x48 */
	/* pca9564_read(&sc, 0x48, size, &buf); */
	buf[0] = 0x00;
	buf[1] = 0x00;
	pca9564_read(&sc, chip_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {
		printf("received (%u): 0x%02x%02x\n", size, buf[0], buf[1]);
		/*printf("read result: 0x%02X, %c0x%02X = %d.%02d%cC\n", buf[0], s, buf[1], buf[0], temperat, 0xb0);*/
		/*printf("LM75 at addr 0x%02x: %c%d.%02d%cC\n", i2c_sensor_addr, s, buf[0], temperat, 0xb0);*/
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
