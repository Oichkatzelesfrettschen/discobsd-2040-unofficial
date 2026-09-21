/* $Header: sig.h,v 7.0 86/10/08 15:13:32 lwall Exp $ */

/* $Log:	sig.h,v $
 * Revision 7.0  86/10/08  15:13:32  lwall
 * Split into separate files.  Added amoebas and pirates.
 * 
 */

_Noreturn void sig_catcher(int);
#ifdef SIGTSTP
void cont_catcher();
void stop_catcher();
#endif
void mytstp();
void sig_init();
_Noreturn void finalize(int);
