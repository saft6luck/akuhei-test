#include <stdio.h>
#include <stdlib.h>

#include <dos/dos.h>
#include <dos/rdargs.h>

#include "akuhei2c.h"

/*#define DEBUG			1 */

/* My custom RDArgs */
struct RDArgs *myrda;

#define TEMPLATE "A=Address/N"
LONG result[1];

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

int main(int argc, char **argv)
{
        pca9564_state_t sc;
        struct Interrupt *int6;

	UBYTE ctrl;
	UBYTE *buf;
	UBYTE size;
	UBYTE p;
	UBYTE s;
	unsigned short temperat;

	UBYTE i2c_sensor_addr;
	LONG *strp;
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
                return 1;
                /* XXX: clean up */
        }

        /* init the host controller */
	/*ctrl = I2CCON_CR_59KHZ | I2CCON_ENSIO;*/
	/*ctrl = I2CCON_CR_88KHZ | I2CCON_ENSIO;*/
	/*ctrl = I2CCON_CR_146KHZ | I2CCON_ENSIO;*/
	/*ctrl = I2CCON_CR_217KHZ | I2CCON_ENSIO;*/
	/*ctrl = I2CCON_CR_288KHZ | I2CCON_ENSIO;*/
	ctrl = I2CCON_CR_330KHZ | I2CCON_ENSIO;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);

	i2c_sensor_addr = 0x48;

	/* read 2 bytes from 0x48 */
	/* pca9564_read(&sc, 0x48, size, &buf); */
	pca9564_read(&sc, i2c_sensor_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {
		s = ' ';
		if (buf[0] & 0x80) {
			buf[0] ^= 0xFF;
			buf[1] ^= 0xFF;
			s = '-';
		}

		temperat = buf[1];
		temperat *= 100;
		temperat >>= 8;

		/*printf("LM75 at addr 0x%02x: %c%d.%02d%cC\n", i2c_sensor_addr, s, buf[0], temperat, 0xb0);*/
		printf("Ambient: %c%d.%02d%cC, ", s, buf[0], temperat, 0xb0);

		clockport_write(&sc, I2CCON, 0);
	}

	++i2c_sensor_addr;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);
	pca9564_read(&sc, i2c_sensor_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {
		s = ' ';
		if (buf[0] & 0x80) {
			buf[0] ^= 0xFF;
			buf[1] ^= 0xFF;
			s = '-';
		}

		temperat = buf[1];
		temperat *= 100;
		temperat >>= 8;

		/*rintf("LM75 at addr 0x%02x: %c%d.%02d%cC\n", i2c_sensor_addr, s, buf[0], temperat, 0xb0);*/
		printf("CPU: %c%d.%02d%cC, ", s, buf[0], temperat, 0xb0);

		clockport_write(&sc, I2CCON, 0);
	}

	++i2c_sensor_addr;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);
	pca9564_read(&sc, i2c_sensor_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {

		printf("PWM %d%%, ", (25600*buf[0] + 256*buf[1])/512);

		clockport_write(&sc, I2CCON, 0);
	}

	++i2c_sensor_addr;
	clockport_write(&sc, I2CCON, ctrl);
	Delay(5);
	pca9564_read(&sc, i2c_sensor_addr, size, &buf);

	if (sc.cur_result == RESULT_OK) {

		printf("Tacho max %dHz, current %dHz\n", buf[0], buf[1]);

		clockport_write(&sc, I2CCON, 0);
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
