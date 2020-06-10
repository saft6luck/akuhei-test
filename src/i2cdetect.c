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
	UBYTE hb, lb;
	int argNo;
	unsigned short temperat;

	UBYTE chip_addr, reg_addr;
	LONG *strp;
	STRPTR sptr;
	UBYTE **arguments;
	UBYTE k;

	/*struct sigaction act;*/

	read_mode_t read_mode;

	size = 2;
	chip_addr = 0;
	reg_addr = 0;
	read_mode = READ_BYTE;

#ifdef DEBUG
	sc.in_isr = FALSE;
	sc.isr_called = 0;
#endif /* DEBUG */
	sc.cp = CLOCKPORT_BASE;
	sc.cur_op = OP_NOP;

	printf("WARNING! This program can confuse your I2C bus, cause data loss and worse!\nI will probe address range 0x03-0x77.\nYou have been warned!\n");

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

	s = clockport_read(&sc, I2CSTA);

	if(s != I2CSTA_IDLE) {
		PutStr("I2C in unappropriate state: \n");
		pca9564_dump_state(&sc);
		FreeMem(buf, size);
		return 1;
	}

	printf("   ");
	for(lb=0; lb<0x10; ++lb)
		printf("%3x", lb);
	printf("\n");
	for(hb=0x00; hb<0x80; hb += 0x10) {
		printf("%02x:", hb);
		for(lb=0x00; lb<0x10; ++lb) {
			chip_addr = hb | lb;
			if((chip_addr > 0x02) && (chip_addr < 0x78)) {
				ctrl = I2CCON_CR_330KHZ | I2CCON_ENSIO;
				clockport_write(&sc, I2CCON, ctrl);
				Delay(5);
				buf[0] = 0x00;
				buf[1] = 0x00;
				pca9564_read(&sc, chip_addr, size, &buf);
				if (sc.cur_result == RESULT_OK) {
					printf(" %02x", chip_addr);
				} else {
					printf(" --");
				}
				Flush(Output());
				/*Flush(IDOS->Output(void));*/
				/*FFlush();*/
				/*Flush(void);*/
				ctrl = 0;
				clockport_write(&sc, I2CCON, ctrl);
				s = clockport_read(&sc, I2CSTA);
				if(s != I2CSTA_IDLE) {
					Delay(50);
					clockport_write(&sc, I2CCON, ctrl);
				} else {
					Delay(5);
				}
			} else
				printf("   ");
		}
		printf("\n");
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
