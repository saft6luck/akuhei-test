#include <stdio.h>
#include <stdlib.h>

#include "akuhei2c.h"

#define DEBUG			1

int main(void)
{
	pca9564_state_t sc;
	struct Interrupt *int6;

	UBYTE ctrl;
	UBYTE *buf;
	UBYTE size;

	size = 2;

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
		/* XXX: clean up */
	} 

	/* init the host controller */
	ctrl = I2CCON_CR_59KHZ | I2CCON_ENSIO;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(50);

	buf[0] = 0x2;
	pca9564_write(&sc, 0x48, 1, &buf); 	/* XXX */
	/* read 2 bytes from 0x48 */
	pca9564_read(&sc, 0x48, size, &buf); 	/* XXX */

	printf("read result: %u, %u\n", buf[0], buf[1]);

	ctrl = 0;
	clockport_write(&sc, I2CCON, ctrl);

	FreeMem(buf, size);

	RemIntServer(INTB_EXTER, int6);
	FreeMem(int6, sizeof(struct Interrupt));
	FreeSignal(sc.sig_intr);

#ifdef DEBUG 
	printf("ISR was called %d times\n", sc.isr_called);
#endif /* DEBUG */
    
	return 0;
}
