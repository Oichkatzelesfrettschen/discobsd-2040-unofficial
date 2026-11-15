#include <sys/types.h>
#include <sys/config.h>

#define C (char *)

extern const struct driver picocalcdriver;
extern const struct driver sddriver;
extern const struct driver uartdriver;
extern const struct driver uartusbdriver;

struct conf_ctlr conf_ctlr_init[] = {
	/* driver,	unit,	addr,		pri,	flags,	alive */
	{ 0 }
};

const struct conf_device conf_device_init[] = {
	/* driver,	ctlr driver,	unit,	ctlr,	drive,	flags */
	{ &picocalcdriver,	0,		-2,	0,	-2,	0x0 },
	{ &sddriver,	0,		-2,	0,	-2,	0x0 },
	{ &uartdriver,	0,		0,	0,	-2,	0x0 },
	{ &uartusbdriver,	0,		-2,	0,	-2,	0x0 },
	{ 0 }
};

const struct conf_service conf_service_init[] = {
	{ 0 }
};
