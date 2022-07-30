#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/*#include <stdint.h>*/

#include "akuhei2c.h"

//#define DEBUG 1
#include <dos/dos.h>
#include <dos/rdargs.h>

#include <signal.h>

/* My custom RDArgs */
struct RDArgs *myrda;

/*#define TEMPLATE "A=Address/A,R=Register,V=Value,W=WordMode/S"*/
#define TEMPLATE "A=Address/A,V=Value(s)/M"
#define OPT_ADDR 0
#define OPT_VALUE  1
#define OPT_WRITEMODE  2
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

	UBYTE chip_addr, reg_value[256];
	LONG *strp;
	STRPTR sptr;
	UBYTE **arguments;
	UBYTE k;

	/*struct sigaction act;*/

	read_mode_t read_mode;

	size = 0;
	chip_addr = 0;
	read_mode = READ_BYTE;

#ifdef DEBUG
	sc.in_isr = FALSE;
	sc.isr_called = 0;
#endif /* DEBUG */
	sc.cp = CLOCKPORT_BASE;
	sc.cur_op = OP_NOP;

#ifdef DEBUG
	printf("Specified parameters (%d):\n", argc);
	for (argNo=0; argNo < argc; ++argNo) {
		printf("   argument #%d = >%s<\n", argNo, argv[argNo]);
	}
#endif // DEBUG

	/* Need to ask DOS for a RDArgs structure */
	if (myrda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) {
	/* parse my command line */
		if (ReadArgs(TEMPLATE, result, myrda) && (strlen((char *)result[0]) > 0)) {
			s = strlen((STRPTR)result[OPT_ADDR]);
			if((s == 2) || (strncmp((STRPTR)result[OPT_ADDR], "0x", 2) == 0) && (s == 4)) {
				chip_addr = stoi((STRPTR)result[OPT_ADDR]);
#ifdef DEBUG
				KPrintF("Chip address Specified : >%s<, len=%d -> 0x%02X\n", (STRPTR)result[OPT_ADDR], strlen((STRPTR)result[OPT_ADDR]), chip_addr);
#endif // DEBUG
			}

			size=0; s=0;
			if(result[OPT_VALUE]) {
				while(((STRPTR*)result[OPT_VALUE])[s]) {
					if(strlen(((STRPTR*)result[OPT_VALUE])[size]) < 3) {
						reg_value[size] = stoi(((STRPTR*)result[OPT_VALUE])[s]);
						printf("VALUE %d: >%s< (%d) => 0x%02X = %d\n", size, ((STRPTR*)result[OPT_VALUE])[size], strlen(((STRPTR*)result[OPT_VALUE])[size]), reg_value[size], reg_value[size]);
						++size;
					} else {
						sptr = ((STRPTR*)result[OPT_VALUE])[s];
						reg_value[size] = 0;
						while(*sptr) {
							reg_value[size] *= 16;
							if((*((const char*)sptr) >= 'a') && (*((const char*)sptr) <= 'f')) {
								reg_value[size] += 10 + *((const char*)sptr) - 'a';
							} else {
								if((*((const char*)sptr) >= 'A') && (*((const char*)sptr) <= 'F')) {
									reg_value[size] += 10 + *((const char*)sptr) - 'A';
								} else {
									reg_value[size] += *((const char*)sptr) - '0';
								}
							}
							if(reg_value[size] > 16) {
								printf("VALUE %d: >XX< => 0x%02X = %d\n", size, reg_value[size], reg_value[size]);
								++size;
								reg_value[size] = 0;
							}
							++sptr;
						}
					}
					++s;
				}
			}

			FreeArgs(myrda);
		} else {
			PutStr("Usage: " TEMPLATE "\n");
			return 1;
		}
		FreeDosObject(DOS_RDARGS, myrda);
//	} else {
//		PutStr("Usage: " TEMPLATE "\n");
	}

//	if((chip_addr < 0x03) || (chip_addr > 0x77)) {
	if(chip_addr > 0x7f) {
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

	if (size && !(buf = AllocMem(size, MEMF_PUBLIC|MEMF_CLEAR))) {
		printf("Not enough memory to allocate the buffer\n");
		FreeSignal(sc.sig_intr);
		return 1;
		/* XXX: clean up */
	}

	for (argNo=0; argNo < size; argNo++) {
		buf[argNo] = (UBYTE)(0xFF & reg_value[argNo]);
	}


	//#ifdef DEBUG
		printf("transmitting (%u bytes):", size);
		for (argNo=0; argNo < size; ++argNo)
			printf("%02d) 0x%02x = %d\n", argNo, reg_value[argNo], reg_value[argNo]);
			//printf("%02d) 0x%02x = %d\n", argNo, buf[argNo], buf[argNo]);
		printf("\n");
	//#endif /* DEBUG */

	/* init the host controller */
	/*ctrl = I2CCON_CR_59KHZ | I2CCON_ENSIO;*/
	ctrl = I2CCON_CR_330KHZ | I2CCON_ENSIO;
	clockport_write(&sc, I2CCON, ctrl);

	if(!chip_addr) {
		clockport_write(&sc, I2CADR, 0x10);
	}
//	Delay(5);


/*	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
	}*/

	//pca9564_write(&sc, chip_addr, size, &reg_value);
	pca9564_write(&sc, chip_addr, size, &buf);

//#ifdef DEBUG
	if (sc.cur_result == RESULT_OK) {
		printf("transmitted (%u): ", size);
		for (argNo=0; argNo < size; ++argNo) {
			printf("%02d) 0x%02x = %d\n", argNo, reg_value[argNo], reg_value[argNo]);
			//printf("%02d) 0x%02x = %d\n", argNo, buf[argNo], buf[argNo]);
		}
	} else {
		printf("received error\n");
	}
//#endif /* DEBUG */

/*	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
	}*/

//	ctrl = 0;
//	clockport_write(&sc, I2CCON, ctrl);

//	pca9564_dump_state(&sc);

	if(size)
		FreeMem(buf, size);

	RemIntServer(INTB_EXTER, int6);
	FreeMem(int6, sizeof(struct Interrupt));
	FreeSignal(sc.sig_intr);

#ifdef DEBUG
	printf("ISR was called %d times\n", sc.isr_called);
	printf("ISR state history %X\n", sc.isr_states);
#endif /* DEBUG */

	return 0;
}
