/*
 * Process contexts for the USB console gate; see usb_console_sched.c. The
 * interface names zero types, so both the kernel translation unit and the host
 * one include it.
 */
#ifndef USB_CONSOLE_SCHED_H
#define USB_CONSOLE_SCHED_H

#define	UCS_NPROC	3

#define	UCS_IDLE	0	/* Never started. */
#define	UCS_RUNNABLE	1
#define	UCS_RUNNING	2
#define	UCS_ASLEEP	3
#define	UCS_DONE	4

void ucs_reset(void);
void ucs_start(int id, void (*body)(void));
int ucs_state(int id);
const void *ucs_channel(int id);
int ucs_current(void);
void ucs_sleep(const void *channel);
void ucs_wakeup(const void *channel);
int ucs_run(void);

#endif /* USB_CONSOLE_SCHED_H */
