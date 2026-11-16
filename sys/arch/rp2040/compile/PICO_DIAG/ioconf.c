#include <sys/types.h>
#include <sys/config.h>

#define C (char *)

extern struct driver uartusbdriver;
extern struct driver uartdriver;

struct conf_ctlr conf_ctlr_init[] = {
	/* driver,	unit,	addr,		pri,	flags,	alive */
	{ 0 }
};

struct conf_device conf_device_init[] = {
	/* driver,	ctlr driver,	unit,	ctlr,	drive,	flags,	pins,	alive */
	{ &uartusbdriver,	0,		-2,	0,	-2,	0x0,	{0},	0 },
	{ &uartdriver,	0,		0,	0,	-2,	0x0,	{0},	0 },
	{ 0 }
};

struct conf_service conf_service_init[] = {
	{ 0 }
};
