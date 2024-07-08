#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <exec/memory.h>

#include "akuhei2c.h"
#include <proto/i2c.h>
#include <libraries/i2c.h>

#include <dos/dos.h>
#include <dos/var.h>
#include <libraries/configvars.h>
#include <dos/rdargs.h>

#include <signal.h>
#include <clib/exec_protos.h>
#include <clib/expansion_protos.h>

struct RDArgs *myrda;
BOOL verbose, debug_print;

#define TEMPLATE "A=Address/A,S=Stride/N,V=Verbose/S,D=Debug/S"
#define OPT_ADDR 0
#define OPT_STRIDE 1
#define OPT_VERBOSE 2
#define OPT_DEBUG 3
LONG result[4];

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

BOOL detect_pca(pca9564_state_t *sc)
{
	UBYTE a, d;

	if(clockport_read(sc, I2CSTA) != I2CSTA_IDLE)
	 	return FALSE;

//	if(clockport_read(sc, I2CADR) != 0x00)
//	 	return FALSE;

//	if(clockport_read(sc, I2CDAT) != 0x00)
//	 	return FALSE;

	a = clockport_read(sc, I2CADR);
//	if(a != 0)
//		return FALSE;
	d = clockport_read(sc, I2CDAT);
//	if(d != 0)
//		return FALSE;

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
	printf("0x%08X  A%02d A%02d 0x%02X 0x%02X 0x%02X 0x%02X", sc->cp, sc->str+1, sc->str,
																								clockport_read(sc, I2CSTA),
																								clockport_read(sc, I2CDAT),
																								clockport_read(sc, I2CADR),
																								clockport_read(sc, I2CCON));
};

UBYTE *cps[] = { (UBYTE *)0xD80000, (UBYTE *)0xD84000, (UBYTE *)0xD88000, (UBYTE *)0xD8C000, (UBYTE *)0xD90000 };

struct Library   *ExpansionBase;
struct Library	 *I2CBase;

int main(int argc, char **argv)
{
	pca9564_state_t sc;
	struct Interrupt *int6;
	struct ConfigDev *myCD;

	UBYTE ctrl;
	UBYTE *buf;
	UBYTE size, p, s, open_cnt;

	UBYTE chip_addr, reg_addr;
	LONG *strp;
	STRPTR sptr;
	UBYTE **arguments;
	UBYTE k, l;
	ULONG ul;
	UBYTE var_cp_name[] = "i2c/cpaddr";
	UBYTE var_cr_name[] = "i2c/cr";
	UBYTE ENV_name[] = "ENV:";
	//                   0123456789012345
	//                  "00D80001 2";
	UBYTE var_value[] = "                ";
	APTR oldwin;

	size = 1;
	chip_addr = 0;
	reg_addr = 0;

#ifdef DEBUG
	sc.in_isr = FALSE;
	sc.isr_called = 0;
#endif /* DEBUG */
	sc.cp = (UBYTE *)CLOCKPORT_BASE;
	sc.str = CLOCKPORT_STRIDE;
	sc.cur_op = OP_NOP;

	printf("Checking if i2c.library is already open.\n");
	I2CBase = OpenLibrary("i2c.library", 39);
  if(I2CBase) {
		open_cnt = I2CBase->lib_OpenCnt;
		printf("Opened with success with %d open count.\n", open_cnt);
		if(open_cnt > 1) {
			printf("i2c.library was already opened %d time(s). Please stop any program using it and unload i2c.library with avail flush command.\n", open_cnt - 1);
		}
		CloseLibrary(I2CBase);
		if(open_cnt > 1)
			return 0;
	} else {
		printf("Open failed.\n");
	}

	verbose = FALSE;
	debug_print = FALSE;

	printf("ClockBase    A1  A0  STA  DAT  ADR  CON detected?\n");

	k = GetVar(var_cr_name, var_value, 2, 0);
	if((k > 0) && (k < 3)) {
		sc.cr = atoh(*var_value);
		printf("Custom i2c clock settings (k = %d): variable i2c_clock set to <<%s>> - 0x%02X.\n", k, var_value, sc.cr);
	} else
		printf("Custom i2c clock not set: k = %d.\n", k);

	if(myrda = (struct RDArgs *)AllocDosObject(DOS_RDARGS, NULL)) {	/* parse my command line */
		ReadArgs(TEMPLATE, result, myrda);

		if(result[OPT_VERBOSE])
			verbose = TRUE;
		if(result[OPT_DEBUG])
		{
			debug_print = TRUE;
			verbose = TRUE;
		}

		if((result[OPT_ADDR] > 0) && (strlen((char *)result[OPT_ADDR]) > 0)) {
			s = strlen((STRPTR)result[OPT_ADDR]);
			if((s == 6) || ((strncmp((STRPTR)result[OPT_ADDR], "0x", 2) == 0) && ((s == 10) || (s == 8)))) {
				sc.cp = (UBYTE *)stol((STRPTR)result[OPT_ADDR]);
				if(result[OPT_STRIDE])
  				sc.str = *((ULONG*)(result[OPT_STRIDE]));
				if(debug_print)
					printf("Attempting to detect PCA at the address provided in command line A=0x%08X, S=%d.\n", sc.cp, sc.str);
				print_pca_state(&sc);
				if(detect_pca(&sc)) {
					printf(" yes\n");
				} else {
					printf(" no\n");
				}
			}
			FreeArgs(myrda);
			FreeDosObject(DOS_RDARGS, myrda);
		} else {
			if(debug_print)
				printf("Attempting to detect PCA at all known addresses in clock port expanders.\nstandard CP\n");
			for(k = 0; k < sizeof(cps)/sizeof(UBYTE*); ++k) {
				// check byte 0xXX...... -> lines D31...D24
				sc.cp = cps[k];
				++(sc.cp);
				// check byte 0x..XX.... -> lines D23...D16
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
				++(sc.cp);
				// check byte 0x....XX.. -> lines D15...D08
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
				// check byte 0x......XX -> lines D7...D0
			}
			sc.str = 12;
			if(debug_print)
				printf("\nCP on Gayle adapter\n");
			for(k = 0; k < sizeof(cps)/sizeof(UBYTE*); ++k) {
				sc.cp = cps[k];
				++(sc.cp);
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
				++(sc.cp);
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
			}
			/* sc.str = 14;
			for(k = 0; k < sizeof(cps)/sizeof(UBYTE*); ++k) {
				sc.cp = cps[k];
				++(sc.cp);
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
				++(sc.cp);
				if(debug_print)
					printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
				if(verbose || detect_pca(&sc)) {
					print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
				}
			} */

			if((ExpansionBase = OpenLibrary("expansion.library",0L))!=NULL)
			{
				if(debug_print)
					printf("Successfully opened expansion.library.\n");
				k = 0;
				myCD = NULL;
				while(myCD = FindConfigDev(myCD,-1L,-1L)) // search for all ConfigDevs
      	{
					if(debug_print)
					{
						printf("ConfigDev structure found at location $%lx---\n",myCD);
						printf("Man: 0x%04X Prd: 0x%02X, Adr: 0x%08lx\n",
												myCD->cd_Rom.er_Manufacturer,
																	myCD->cd_Rom.er_Product,
																							myCD->cd_BoardAddr);
					}
					++k;
					// Prisma Megamix Zorro card with clockport
					if((myCD->cd_Rom.er_Manufacturer == 0x0E3B) && (myCD->cd_Rom.er_Product == 0x30)) {
						sc.str = 2;
						sc.cp = (UBYTE *)(myCD->cd_BoardAddr + 0x00004000);
						if(debug_print)
							printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
						if(verbose || detect_pca(&sc)) {
							print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Prisma Megamix\n");
						}
					}
					// Icomp card with clockport
					// 0x1212:0x05 ISDN Surfer
					// 0x1212:0x07 VarIO
					// 0x1212:0x0A KickFlash
					if(myCD->cd_Rom.er_Manufacturer == 0x1212)
					{
						if ((myCD->cd_Rom.er_Product == 0x05)
						||  (myCD->cd_Rom.er_Product == 0x07)
						||  (myCD->cd_Rom.er_Product == 0x0A))
						{
							sc.str = 2;
							sc.cp = (UBYTE *)(myCD->cd_BoardAddr + 0x00008000);
							if(myCD->cd_Rom.er_Product == 0x0A) {
								// activate CP for KickFlasher
								buf = sc.cp + 0x007C;
								*buf = 0xFF;
							}
							if(debug_print)
								printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
							if(verbose || detect_pca(&sc)) {
								print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Icomp/");
								switch (myCD->cd_Rom.er_Product) {
									case 0x05: printf("ISDN Surfer"); break;
									case 0x07: printf("VarIO"); break;
									case 0x0A: printf("KickFlash"); break;
								}
								printf(".\n");
							}
						}
						if(myCD->cd_Rom.er_Product == 0x17)
						{
							sc.str = 2;
							sc.cp = (UBYTE *)(myCD->cd_BoardAddr + 0x0000c000);
							if(debug_print)
								printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
							if(verbose || detect_pca(&sc)) {
								print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Icomp/X-Surfer Port0\n");
							}
							sc.cp = (UBYTE *)(myCD->cd_BoardAddr + 0x0000a001);
							if(debug_print)
								printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
							if(verbose || detect_pca(&sc)) {
								print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Icomp/X-Surfer Port1\n");
							}
						}
						if(myCD->cd_Rom.er_Product == 0x00)
						{
							sc.str = 2;
							sc.cp = (UBYTE *)(myCD->cd_BoardAddr + 0x00000e00);
							if(debug_print)
								printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
							if(verbose || detect_pca(&sc)) {
								print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Icomp/Buddha\n");
							}
						}
					}
					// A1K.org Community
					// A LAN/IDE solluntion with Clockport for the Amiga ZorroII/III Slot
					// Matthias Heinrichs
					if((myCD->cd_Rom.er_Manufacturer == 0x0A1C) && (myCD->cd_Rom.er_Product == 0x7C)) {
						sc.str = 2;
						sc.cp = (UBYTE *)(myCD->cd_BoardAddr);
						if(debug_print)
							printf("trying base: 0x%08X with A1: A%02d and A0: A%02d\n", sc.cp, sc.str+1, sc.str);
						if(verbose || detect_pca(&sc)) {
							print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf(" <- Matzes Clockport\n");
						}
					}
				}
				printf("found %d Zorro board%s.\n", k, (k>1)?"s":"");
				CloseLibrary(ExpansionBase);
			}

			//DOSBase = (struct DosLibrary *)OpenLibrary("dos.library",0L);
			/* stop any "Insert volume..." type requesters */
			//oldwin = SetProcWindow((APTR)-1);

			struct Process *proc;

			proc=(struct Process *)FindTask(0);
    	proc->pr_WindowPtr=(APTR)-1;         // hide sys. reqs

			k = Lock(ENV_name, SHARED_LOCK);
			/* turn requesters back on */
			//SetProcWindow(oldwin);
			printf("Lock attempt on ENV returned %x\n", k);
			if(k)
			{
				//                name, buffer, size, flags
				// LONG GetVar( STRPTR, STRPTR, LONG, ULONG )
				sc.str = -1;
				sc.cp = (UBYTE*)-1;
				k = GetVar(var_cp_name, var_value, 16, 0);
				if((k > 8) && (k < 16)) {
					//printf("k = %d.\n", k);
					//k = sscanf(var_value, "%8x:%d", &sc.cp, &ul);
					buf = var_value;
					ul = 0;
					for(l = 0; l < 8; ++l, ++buf) {
						ul <<= 4;
						ul += atoh(*buf);
					}
					sc.cp = (UBYTE*)ul;
					sc.str = atoh(*buf);
					if(k > 9) {
						++buf;
						sc.str <<= 4;
						sc.str += atoh(*buf);
					}
					printf("Custom settings: CP addr 0x%08x and stride %d.\n", sc.cp, sc.str);
					if((sc.str < 31) && (verbose || detect_pca(&sc))) {
						print_pca_state(&sc); printf(" "); printf(detect_pca(&sc)?"yes":"no"); printf("\n");
					}
				}
			}
		}
	}

	return 0;
}
