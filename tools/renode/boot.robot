*** Comments ***
The boot gate. It asserts the console lines the kernel prints between the
boot ROM's jump into boot2 and the end of the device probe, which is as far
as the emulator gets: Renode's RP2040 SSI model shares its FIFOs across two
threads without a lock and eventually loses a byte, after which the boot ROM
waits under splhigh() for data that will not arrive.
sys/arch/rp2040/doc/research/emulation.md has the measurements.

What the assertions below cover is still most of bring-up. Reaching
"swap size" means boot2 configured the SSI for XIP and jumped to 0x10000100,
SystemClock_Config finished rather than spinning, rom_func_lookup() resolved
the erase and program pointers through the real boot ROM table, Dhara resumed
its journal through them, and the partition and swap geometry came back the
size the image was built with. A regression in any of those stops a line
from arriving.

The sizes are asserted rather than only the line names, so a change to the
flash layout that nothing else notices fails here.

*** Settings ***
Suite Setup                     Setup
Suite Teardown                  Teardown
Test Teardown                   Test Teardown
Test Timeout                    180 seconds

*** Variables ***
${MACHINE_SCRIPT}               ${CURDIR}/machine.resc

*** Test Cases ***
The kernel boots through its device probe
    Execute Command             include @${MACHINE_SCRIPT}
    Create Terminal Tester      sysbus.uart0
    Start Emulation

    Wait For Line On Uart       DiscoBSD 2.7 (PICO_UART)                    timeout=60
    Wait For Line On Uart       cpu: Cortex-M0+, ARMv6-M, no MMU and no MPU    timeout=60
    Wait For Line On Uart       cpu: 125 MHz core, 125 MHz peripheral       timeout=60

    # fl0 prints only after rom_func_lookup() has resolved the ROM's flash
    # entry points and Dhara has resumed its journal through them.
    Wait For Line On Uart       fl0: 989 kbytes on QSPI flash, 1536 kbytes raw    timeout=60
    Wait For Line On Uart       fl0a: partition type b7, sector 2, size 988 kbytes    timeout=60
    Wait For Line On Uart       fl1: 384 kbytes raw QSPI flash for swap     timeout=60

    Wait For Line On Uart       phys mem${SPACE}${SPACE}= 264 kbytes                      timeout=60
    Wait For Line On Uart       user mem${SPACE}${SPACE}= 144 kbytes                      timeout=60
    Wait For Line On Uart       root dev${SPACE}${SPACE}= (0,1)                           timeout=60
    Wait For Line On Uart       swap dev${SPACE}${SPACE}= (0,8)                           timeout=60
    Wait For Line On Uart       root size = 988 kbytes                      timeout=60
    Wait For Line On Uart       swap size = 380 kbytes                      timeout=60
