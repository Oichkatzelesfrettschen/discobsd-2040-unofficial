"""Check linked RP2040 exception-vector slots against their named symbols."""

import argparse
import io
import struct
import sys
from pathlib import Path

from check_rp2040_execsw import offset_for, symbol
from elftools.common.exceptions import ELFError
from elftools.elf.elffile import ELFFile

VECTOR_ADDRESS = 0x10000100
SLOTS = (
    "_estack", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
    None, None, None, None, None, None, None, "SVC_Handler", None, None,
    "PendSV_Handler", "SysTick_Handler",
    "TIMER_IRQ_0_Handler", "TIMER_IRQ_1_Handler", "TIMER_IRQ_2_Handler",
    "TIMER_IRQ_3_Handler", "PWM_IRQ_WRAP_Handler", "USBCTRL_IRQ_Handler",
    "XIP_IRQ_Handler", "PIO0_IRQ_0_Handler", "PIO0_IRQ_1_Handler",
    "PIO1_IRQ_0_Handler", "PIO1_IRQ_1_Handler", "DMA_IRQ_0_Handler",
    "DMA_IRQ_1_Handler", "IO_IRQ_BANK0_Handler", "IO_IRQ_QSPI_Handler",
    "SIO_IRQ_PROC0_Handler", "SIO_IRQ_PROC1_Handler", "CLOCKS_IRQ_Handler",
    "SPI0_IRQ_Handler", "SPI1_IRQ_Handler", "UART0_IRQ_Handler", "UART1_IRQ_Handler",
    "ADC_IRQ_FIFO_Handler", "I2C0_IRQ_Handler", "I2C1_IRQ_Handler", "RTC_IRQ_Handler",
    "SPARE_IRQ_0_Handler", "SPARE_IRQ_1_Handler", "SPARE_IRQ_2_Handler",
    "SPARE_IRQ_3_Handler", "SPARE_IRQ_4_Handler", "SPARE_IRQ_5_Handler",
)


def check(path):
    data = path.read_bytes()
    elf = ELFFile(io.BytesIO(data))
    if (elf.elfclass != 32 or not elf.little_endian or elf.header.e_machine != "EM_ARM" or
            elf.header.e_type != "ET_EXEC"):
        raise ValueError("expected a linked little-endian ARM32 ELF")
    table = elf.get_section_by_name(".symtab")
    if table is None or table.header.sh_type != "SHT_SYMTAB":
        raise ValueError("symbol table is absent")
    address = symbol(table, "g_pfnVectors", "STT_OBJECT", len(SLOTS) * 4)
    if address != VECTOR_ADDRESS:
        raise ValueError("vector table address differs from the boot2 entry")
    offset = offset_for(elf, data, address, len(SLOTS) * 4)
    words = struct.unpack_from(f"<{len(SLOTS)}I", data, offset)
    for index, (name, value) in enumerate(zip(SLOTS, words)):
        if name is None:
            if value != 0:
                raise ValueError(f"reserved vector {index} is nonzero")
            continue
        kind = "STT_NOTYPE" if index == 0 else "STT_FUNC"
        expected = symbol(table, name, kind)
        if value != expected:
            raise ValueError(f"vector {index} differs from {name}")
        if index == 0:
            if value & 7:
                raise ValueError("initial stack pointer is not eight-byte aligned")
        else:
            if not value & 1:
                raise ValueError(f"vector {index} lacks the Thumb bit")
            offset_for(elf, data, value & ~1, 2, executable=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("kernels", nargs="+", type=Path)
    args = parser.parse_args()
    try:
        for kernel in args.kernels:
            check(kernel)
            print(f"PASS RP2040 vectors: {kernel}")
    except (OSError, ValueError, ELFError, struct.error, IndexError, KeyError, TypeError) as error:
        print(f"FAIL RP2040 vectors: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
