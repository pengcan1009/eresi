#include "kedbg.h"
#include "interface.h"


/**
 * This function emulates the "monitor r cr0" we can send in VMWare to
 * check the cr0 register. However, when the server replies, the first
 * 0 has to be discarded.
 */
static Bool     kedbg_isrealmodewmon(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  char          code[] = "r cr0";
  char          reply[50];
  char          *ret;
  uint8_t       i;

  PROFILER_INQ();
  ret = gdbwrap_remotecmd(loc, code);

  if (gdbwrap_cmdnotsup(loc))
    PROFILER_ROUTQ(FALSE);
 
  /* We add +1 to discard the first char.*/
  for (i = 0; ret[i] != '\0'; i++)
    reply[i] = (char)gdbwrap_atoh(ret + BYTE_IN_CHAR * i + 1, 2);

  /* Last bit is 0. */  
  if (!(gdbwrap_atoh(reply + strlen(reply) - 2, 1) & 0x1))
    {
      kedbgworld.pmode = FALSE;
      PROFILER_ROUTQ(TRUE);
    }
  else
    {
      kedbgworld.pmode = TRUE;
      PROFILER_ROUTQ(FALSE);
    }
}


/* This is absolutely not working. */
static Bool     kedbg_isrealmodeinject(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  char          code[]="\x0f\x20\xc0";
  char          saved[4];
  ureg32        savedeip;
  Bool          ret;
  
  PROFILER_INQ();
  printf("Saved eip: %#x\n", loc->reg32.eip);
  savedeip = loc->reg32.eip;
  printf("sizeof du code: %d\n", 3);
  kedbg_readmema(NULL, savedeip, saved, 3);
  kedbg_writemem(NULL, savedeip, code, strlen(code));
  gdbwrap_stepi(loc);
  gdbwrap_readgenreg(loc);
  printf("Value of eax: %#x\n", loc->reg32.eax);
  ret = (loc->reg32.eax & 0x1) ? TRUE : FALSE;
  kedbg_writemem(NULL, savedeip, saved, 3);
  gdbwrap_writereg2(loc, 8, savedeip);

  PROFILER_ROUTQ(ret);
}


Bool           kedbg_isrealmode(void)
{
  static u_char choice = 0;
  gdbwrap_t     *loc = gdbwrap_current_get();
  Bool          ret;
  
  PROFILER_INQ();

  /* If we are not running in a VM, we've nothing to do here. */
  if (!kedbgworld.run_in_vm || kedbgworld.pmode)
    PROFILER_ROUTQ(FALSE);
 
  do
    {
      switch (choice)
	{
	  case 0:
	    ret = kedbg_isrealmodewmon();
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  case 1:
	    ret = kedbg_isrealmodeinject();
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  default:
	    fprintf(stderr, "Impossible to determine the mode.\n");
	    break;
	}
    } while (gdbwrap_cmdnotsup(loc));

  PROFILER_ROUTQ(ret);
}


void            kedbg_resetstep(void)
{
  PROFILER_INQ();
  e2dbgworld.curthread->step = FALSE;

  PROFILER_OUTQ();
}


void            kedbg_setstep(void)
{
  PROFILER_IN(__FILE__, __FUNCTION__, __LINE__);  
  e2dbgworld.curthread->step = TRUE;

  PROFILER_OUTQ();
}


void		*kedbg_bt_ia32(void *frame)
{
  la32          ptr = 0;

  PROFILER_INQ();
  
  if (kedbg_isrealmode())
    kedbg_readmema(NULL,(eresi_Addr)frame, &ptr, WORD_IN_BYTE);
  else
    kedbg_readmema(NULL,(eresi_Addr)frame, &ptr, DWORD_IN_BYTE);
  
  PROFILER_ROUTQ((void *)ptr);
}


eresi_Addr	*kedbg_getfp(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  /* First update all the reg. */
  gdbwrap_readgenreg(loc);
  PROFILER_ROUTQ((eresi_Addr *)loc->reg32.ebp);
}


void            *kedbg_getret_ia32(void *frame)
{
  la32          ptr = 0;

  PROFILER_INQ();
  if (kedbg_isrealmode())
    kedbg_readmema(NULL, (la32)((la32 *)frame + 1), &ptr, WORD_IN_BYTE);
  else
    kedbg_readmema(NULL, (la32)((la32 *)frame + 1), &ptr, DWORD_IN_BYTE);
  //  kedbg_readmema(NULL, (la32)((la32 *)frame + 1), &ptr, DWORD_IN_BYTE);
  PROFILER_ROUT(__FILE__, __FUNCTION__, __LINE__, (void *)ptr);
}


static int      kedbg_simplesetbp(elfshobj_t *f, elfshbp_t *bp)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  NOT_USED(f);
  gdbwrap_simplesetbp(loc, bp->addr);

  PROFILER_ROUTQ(0);
}


static int      kedbg_setbpwint3(elfshobj_t *f, elfshbp_t *bp)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  NOT_USED(f);
  gdbwrap_setbp(loc, bp->addr, bp->savedinstr);

  PROFILER_ROUTQ(0);
}


/**
 * Set up a breakpoint. We have to change the memory on the server
 * side, thus we need to save the value we change.
 */
int             kedbg_setbp(elfshobj_t *f, elfshbp_t *bp)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  static u_char choice = 0;

  PROFILER_INQ();
  do
    {
      switch (choice)
	{
	  case 0:
	    kedbg_simplesetbp(f, bp);
	    kedbgworld.offset = 0;
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  case 1:
	    kedbg_setbpwint3(f, bp);
	    kedbgworld.offset = 1;
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  default:
	    /* We gonna loop, but should not occur */
	    fprintf(stderr, "Bp not supported - Muh ? \n");
	    break;
	}
    } while (gdbwrap_cmdnotsup(loc));

  PROFILER_ROUTQ(0);
}


static int      kedbg_delbpwint3(elfshbp_t *bp)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  gdbwrap_delbp(loc, bp->addr, bp->savedinstr);

  PROFILER_ROUTQ(0);
}


static int      kedbg_simpledelbp(elfshbp_t *bp)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  gdbwrap_simpledelbp(loc, bp->addr);

  PROFILER_ROUTQ(0);
}


int             kedbg_delbp(elfshbp_t *bp)
{
  PROFILER_INQ();

  if (!kedbgworld.offset)
    kedbg_simpledelbp(bp);
  else if (kedbgworld.offset == 1)
    kedbg_delbpwint3(bp);
  else
    ASSERT(FALSE);
  
  PROFILER_ROUTQ(0);
}


void            kedbg_print_reg(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  gdbwrap_gdbreg32 *reg;
    
  PROFILER_INQ();
  reg = &loc->reg32;
  e2dbg_register_dump("EAX", reg->eax);
  e2dbg_register_dump("EBX", reg->ebx);
  e2dbg_register_dump("ECX", reg->ecx);
  e2dbg_register_dump("EDX", reg->edx);
  e2dbg_register_dump("ESI", reg->esi);
  e2dbg_register_dump("EDI", reg->edi);
  e2dbg_register_dump("ESP", reg->esp);
  e2dbg_register_dump("EBP", reg->ebp);
  e2dbg_register_dump("EIP", reg->eip);
  e2dbg_register_dump("EFLAGS", reg->eflags);
  e2dbg_register_dump("CS", reg->cs);
  e2dbg_register_dump("DS", reg->ds);
  e2dbg_register_dump("SS", reg->ss);
  e2dbg_register_dump("ES", reg->es);
  e2dbg_register_dump("FS", reg->fs);
  e2dbg_register_dump("GS", reg->gs);

  PROFILER_OUTQ();
}


void            kedbg_sigint(int sig)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  
  PROFILER_INQ();
  NOT_USED(sig);
  gdbwrap_ctrl_c(loc);
  kedbgworld.interrupted = TRUE;
  PROFILER_OUTQ();
}


void            *kedbg_readmema(elfshobj_t *file, eresi_Addr addr,
				void *buf, u_int size)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  char          *ret;
  char          *charbuf = buf;
  u_int         i;

  PROFILER_INQ();
  NOT_USED(file);
  ret = gdbwrap_readmemory(loc, addr, size);

  /* gdbserver sends a string, we need to convert it. Note that 2
     characters = 1 real Byte.*/
  for (i = 0; i < size; i++) 
    charbuf[i] = (u_char)gdbwrap_atoh(ret + 2 * i, 2);

  PROFILER_ROUTQ(charbuf);
}


void            *kedbg_readmem(elfshsect_t *sect)
{
  void *ptr;

  PROFILER_INQ();
  if(!elfsh_get_section_writableflag(sect->shdr))
    ptr = sect->data;
  else
    {
      ptr = malloc(sect->shdr->sh_size);
      kedbg_readmema(sect->parent, sect->shdr->sh_addr, ptr,
		     sect->shdr->sh_size);
    }

  PROFILER_ROUTQ(ptr);
}


int             kedbg_writemem(elfshobj_t *file, eresi_Addr addr, void *data,
			       u_int size)
{
  static u_char choice = 0;
  gdbwrap_t     *loc = gdbwrap_current_get();
  
  PROFILER_INQ();
  NOT_USED(file);
    do
    {
      switch (choice)
	{
	  case 0:
	    gdbwrap_writememory(loc, addr, data, size);
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  case 1:
	    gdbwrap_writememory2(loc, addr, data, size);
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  default:
	    fprintf(stderr, "Write to memory not supported.\n");
	    break;
	}
    } while (gdbwrap_cmdnotsup(loc));
    
  PROFILER_ROUTQ(0);
}


eresi_Addr      *kedbg_getpc_ia32(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  /* First update all the reg. */
  gdbwrap_readgenreg(loc);
  PROFILER_ROUTQ((eresi_Addr *)&loc->reg32.eip);
}


/**
 * We first sync the registers with the server, then we write the new
 * set ones. They'll be sent to the server when we'll do a "cont".
 */
void            kedbg_set_regvars_ia32(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  gdbwrap_gdbreg32   *reg;

  PROFILER_INQ();
  reg =  gdbwrap_readgenreg(loc);
  if (reg != NULL)
    {

      E2DBG_SETREG(E2DBG_EAX_VAR, reg->eax);
      E2DBG_SETREG(E2DBG_EBX_VAR, reg->ebx);
      E2DBG_SETREG(E2DBG_ECX_VAR, reg->ecx);
      E2DBG_SETREG(E2DBG_EDX_VAR, reg->edx);
      E2DBG_SETREG(E2DBG_ESI_VAR, reg->esi);
      E2DBG_SETREG(E2DBG_EDI_VAR, reg->edi);
      E2DBG_SETREG(E2DBG_SP_VAR,  reg->esp);
      E2DBG_SETREG(E2DBG_FP_VAR,  reg->ebp);
      E2DBG_SETREG(E2DBG_PC_VAR,  reg->eip);
    }
  PROFILER_OUTQ();
}


void            kedbg_get_regvars_ia32(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();

  PROFILER_INQ();
  gdbwrap_readgenreg(loc);
  

  E2DBG_GETREG(E2DBG_EAX_VAR, loc->reg32.eax);
  E2DBG_GETREG(E2DBG_EBX_VAR, loc->reg32.ebx);
  E2DBG_GETREG(E2DBG_ECX_VAR, loc->reg32.ecx);
  E2DBG_GETREG(E2DBG_EDX_VAR, loc->reg32.edx);
  E2DBG_GETREG(E2DBG_ESI_VAR, loc->reg32.esi);
  E2DBG_GETREG(E2DBG_EDI_VAR, loc->reg32.edi);
  E2DBG_GETREG(E2DBG_SP_VAR,  loc->reg32.esp);
  //E2DBG_GETREG(E2DBG_SSP_VAR, loc->reg32.eax);
  E2DBG_GETREG(E2DBG_FP_VAR,  loc->reg32.ebp);
  E2DBG_GETREG(E2DBG_PC_VAR,  loc->reg32.eip);
  PROFILER_OUTQ();
}


void            kedbg_shipallreg(void)
{
  gdbwrap_t     *loc = gdbwrap_current_get();
  static Bool   activated  = TRUE;

  if (activated)
    {
      gdbwrap_shipallreg(loc);
      if (gdbwrap_erroroccured(loc))
	{
	  fprintf(stderr, "Desactivating the set registers"
		  "(not supported by the server).\n");
	  activated = FALSE;
	}
    }
}


int             kedbg_writereg(ureg32 regNum, la32 val)
{
  static u_char choice = 0;
  gdbwrap_t     *loc = gdbwrap_current_get();
  
  PROFILER_INQ();
  do
    {
      switch (choice)
	{
	  case 0:
	    gdbwrap_writereg(loc, regNum, val);
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  case 1:
	    gdbwrap_writereg2(loc, regNum, val);
	    if (gdbwrap_cmdnotsup(loc))
	      choice++;
	    break;

	  default:
	    fprintf(stderr, "Write to registers not supported.\n");
	    break;
	}
    } while (gdbwrap_cmdnotsup(loc));
    
  PROFILER_ROUTQ(0);

}