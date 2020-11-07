/*
  Hatari - hatari-glue.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  This file contains some code to glue the UAE CPU core to the rest of the
  emulator and Hatari's "illegal" opcodes.
*/
const char HatariGlue_fileid[] = "Hatari hatari-glue.c";


#include <stdio.h>

#include "main.h"
#include "configuration.h"
#include "cycInt.h"
#include "tos.h"
#include "gemdos.h"
#include "natfeats.h"
#include "cart.h"
#include "vdi.h"
#include "stMemory.h"
#include "ikbd.h"
#include "screen.h"
#include "video.h"
#include "psg.h"
#include "mfp.h"
#include "fdc.h"

#include "sysdeps.h"
#include "maccess.h"
#include "memory.h"
#include "m68000.h"
#include "newcpu.h"
#include "hatari-glue.h"


struct uae_prefs currprefs, changed_prefs;

int pendingInterrupts = 0;


/**
 * Reset custom chips
 * In case the RESET instruction is called, we must reset all the peripherals
 * connected to the CPU's reset pin.
 */
void customreset(void)
{
	pendingInterrupts = 0;

	/* Reset the IKBD */
	IKBD_Reset ( false );

	/* Resetting the GLUE video chip should also set freq/res register to 0 */
	Video_Reset_Glue ();

	/* Reset the YM2149 (stop any sound) */
	PSG_Reset ();

	/* Reset the MFP */
	MFP_Reset_All ();

	/* Reset the FDC */
	FDC_Reset ( false );
}


/**
 * Return interrupt number (1 - 7), -1 means no interrupt.
 * Note that the interrupt stays pending if it can't be executed yet
 * due to the interrupt level field in the SR.
 */
int intlev(void)
{
	/* There are only VBL and HBL autovector interrupts in the ST... */
	assert((pendingInterrupts & ~((1<<4)|(1<<2))) == 0);

#if 0
	if (pendingInterrupts & (1 << 4))         /* VBL interrupt? */
	{
		if (regs.intmask < 4)
			pendingInterrupts &= ~(1 << 4);
		return 4;
	}
	else if (pendingInterrupts & (1 << 2))    /* HBL interrupt? */
	{
		if (regs.intmask < 2)
			pendingInterrupts &= ~(1 << 2);
		return 2;
	}
#else
	if ( pendingInterrupts & (1 << 4) )		/* VBL interrupt ? */
		return 4;
	else if ( pendingInterrupts & (1 << 2) )	/* HBL interrupt ? */
		return 2;
#endif

	return -1;
}


/**
 * Initialize 680x0 emulation
 */
int Init680x0(void)
{
	currprefs.cpu_level = changed_prefs.cpu_level = ConfigureParams.System.nCpuLevel;
	currprefs.cpu_compatible = changed_prefs.cpu_compatible = ConfigureParams.System.bCompatibleCpu;
	currprefs.address_space_24 = changed_prefs.address_space_24 = true;

	init_m68k();
	M68000_PatchCpuTables();

	return true;
}


/**
 * Deinitialize 680x0 emulation
 */
void Exit680x0(void)
{
	memory_uninit();

	free(table68k);
	table68k = NULL;
}


/**
 * Execute a 'NOP' opcode (increment PC by 2 bytes and take care
 * of prefetch at the CPU level depending on the current CPU mode)
 * This is used to return from SysInit / Natfeats interception, by ignoring
 * the intercepted opcode and executing a NOP instead once the work has been done.
 */
static void	CpuDoNOP ( void )
{
	(*cpufunctbl[0X4E71])(0x4E71);
}


/**
 * Check if the CPU type has been changed
 */
void check_prefs_changed_cpu(void)
{
	if (currprefs.cpu_level != changed_prefs.cpu_level
	                || currprefs.cpu_compatible != changed_prefs.cpu_compatible)
	{
		currprefs.cpu_level = changed_prefs.cpu_level;
		currprefs.cpu_compatible = changed_prefs.cpu_compatible;
		set_special(SPCFLAG_MODE_CHANGE);
		build_cpufunctbl ();
		M68000_PatchCpuTables();
	}
}


/**
 * Check whether PC is currently in ROM cartridge space - used
 * to test whether our "illegal" Hatari opcodes should be handled
 * or whether they are just "normal" illegal opcodes.
 */
static bool is_cart_pc(void)
{
	Uint32 pc = M68000_GetPC() & 0x00ffffff;
	return pc >= 0xfa0000 && pc < 0xfc0000;
}


/**
 * This function will be called at system init by the cartridge routine
 * (after gemdos init, before booting floppies).
 * The GEMDOS vector (#$84) is setup and we also initialize the connected
 * drive mask and Line-A  variables (for an extended VDI resolution) from here.
 */
unsigned long OpCode_SysInit(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		/* Add any drives mapped by TOS in the interim */
		ConnectedDriveMask |= STMemory_ReadLong(0x4c2);
		/* Initialize the connected drive mask */
		STMemory_WriteLong(0x4c2, ConnectedDriveMask);

		/* Init on boot - see cart.c */
		GemDOS_Boot();

		/* Update LineA for extended VDI res
		 * D0: LineA base, A1: Font base
		 */
		VDI_LineA(regs.regs[0], regs.regs[9]);

		CpuDoNOP ();
	}
	else if (!bUseTos)
	{
		GemDOS_Boot();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS | TRACE_OS_BASE | TRACE_OS_VDI | TRACE_OS_AES,
			  "SYSINIT opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch_0();
	}

	return 4;
}


/**
 * Intercept GEMDOS calls.
 * Used for GEMDOS HD emulation (see gemdos.c).
 */
unsigned long OpCode_GemDos(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		GemDOS_Trap();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "GEMDOS opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch_0();
	}

	return 4;
}

/**
 * Handle illegal opcode #9 (PEXEC_OPCODE).
 * When GEMDOS HD emulation is enabled, we use it to intercept the end of
 * the Pexec call (see gemdos.c).
 */
unsigned long OpCode_Pexec(uae_u32 opcode)
{
	if (is_cart_pc())
	{
		GemDOS_PexecBpCreated();
		CpuDoNOP();
	}
	else
	{
		LOG_TRACE(TRACE_OS_GEMDOS, "PEXEC opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
		fill_prefetch_0();
	}

	return 4;
}


/**
 * This is called after completion of each VDI call
 */
unsigned long OpCode_VDI(uae_u32 opcode)
{
	/* this is valid only after VDI trap, called from cartridge code */
	if (VDI_OldPC && is_cart_pc())
	{
		VDI_Complete();

		/* Set PC back to where originated from to continue instruction decoding */
		m68k_setpc(VDI_OldPC);
		VDI_OldPC = 0;
	}
	else
	{
		LOG_TRACE(TRACE_OS_VDI, "VDI opcode invoked outside of cartridge space\n");
		/* illegal instruction */
		op_illg(opcode);
	}
	fill_prefetch_0();
	return 4;
}


/**
 * Emulator Native Features ID opcode interception.
 */
unsigned long OpCode_NatFeat_ID(uae_u32 opcode)
{
	Uint32 stack = Regs[REG_A7] + SIZE_LONG;	/* skip return address */

	if (NatFeat_ID(stack, &(Regs[REG_D0]))) {
		CpuDoNOP ();
	}
	return 4;
}

/**
 * Emulator Native Features call opcode interception.
 */
unsigned long OpCode_NatFeat_Call(uae_u32 opcode)
{
	Uint32 stack = Regs[REG_A7] + SIZE_LONG;	/* skip return address */
	Uint16 SR = M68000_GetSR();
	bool super;
	
	super = ((SR & SR_SUPERMODE) == SR_SUPERMODE);
	if (NatFeat_Call(stack, super, &(Regs[REG_D0]))) {
		CpuDoNOP ();
	}
	return 4;
}
