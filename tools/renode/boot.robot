*** Comments ***
The boot gate. Two tests: the first asserts the console lines the kernel
prints between the boot ROM's jump into boot2 and the end of the device
probe, the second carries on through userland to a shell prompt.

The device-probe test isolates bring-up. Reaching "swap size" means boot2
configured the SSI for XIP and jumped to 0x10000100, SystemClock_Config
finished rather than spinning, rom_func_lookup() resolved the erase and
program pointers through the real boot ROM table, Dhara resumed its journal
through them, and the partition and swap geometry came back the size the
image was built with. A regression in any of those stops a line from
arriving. The sizes are asserted rather than only the line names, so a
change to the flash layout that nothing else notices fails here.

The login test covers what the device probe cannot: exec of a compressed
a.out through the swap writer, the tty layer with a console a process can
open, fork and wait, and the filesystem under write traffic. It logs in and
runs a program, because a prompt alone would also appear from a shell that
cannot execute anything.

Neither test decides SSI concurrency. Create Terminal Tester pauses the
emulation at every wait, which serializes the two CPU threads enough that a
race between them rarely fires: this suite passed against a model whose
BUSY flag latched and hung every free-running boot.
sys/arch/rp2040/doc/research/emulation.md carries the free-running
measurement that does decide it.

Both tests write a Renode log into vendor/results, which check-boot.sh
hands to check-warnings.py.

The terminal tester records only the lines a test waits for, so a kernel
that panics and stops at "press any key to reboot..." shows as a timeout
with nothing to read. The teardown logs the UART's whole history buffer
when a test fails, so the panic text is in the run output.

*** Settings ***
Suite Setup                     Setup
Suite Teardown                  Teardown
Test Teardown                   Teardown With Transcript
Test Timeout                    600 seconds

*** Variables ***
${MACHINE_SCRIPT}               ${CURDIR}/machine.resc
${LOG_DIR}                      ${CURDIR}/vendor/results

*** Test Cases ***
The kernel boots through its device probe
    Execute Command             include @${MACHINE_SCRIPT}
    Execute Command             logFile @${LOG_DIR}/probe.log true
    Create Terminal Tester      sysbus.uart0
    Start Emulation

    Wait For Line On Uart       DiscoBSD 2.7 (PICO_UART)                    timeout=60
    Wait For Line On Uart       cpu: Cortex-M0+, ARMv6-M, no MMU, MPU unprogrammed    timeout=60
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

The boot reaches a login prompt and a shell
    Execute Command             include @${MACHINE_SCRIPT}
    Execute Command             logFile @${LOG_DIR}/login.log true
    Create Terminal Tester      sysbus.uart0
    Start Emulation

    Wait For Line On Uart       swap size = 380 kbytes                      timeout=120

    # fsck reaching its summary line means it mounted the root and walked it
    # without going interactive. The counts in that line follow the manifest
    # and move whenever a program is added or dropped, so they stay out of
    # the assertion; the flash geometry is the probe test's job.
    Wait For Line On Uart       Automatic boot in progress: starting file system checks.    timeout=180
    Wait For Line On Uart       /dev/fl0a:                                  timeout=300
    Wait For Line On Uart       Starting daemons:${SPACE}${SPACE}update                   timeout=300

    # init opens /dev/console and forks getty, which needs a tty a process
    # can open rather than the kernel's own cnputc path.
    Wait For Prompt On Uart     login:                                      timeout=300
    Write Line To Uart          operator
    Wait For Prompt On Uart     $                                           timeout=300

    Write Line To Uart          uname -sr
    Wait For Line On Uart       DiscoBSD 2.7                                timeout=300

*** Keywords ***
Teardown With Transcript
    Run Keyword If Test Failed  Log Uart Transcript
    Test Teardown

Log Uart Transcript
    ${transcript}=  Execute Command  sysbus.uart0 DumpHistoryBuffer
    Log To Console              UART transcript at failure:${\n}${transcript}
    Log                         ${transcript}
