#include "SYS.h"

ENTRY(shared_syscall_error)
	SYS_ERROR
END(shared_syscall_error)
.section .note.GNU-stack,"",%progbits
