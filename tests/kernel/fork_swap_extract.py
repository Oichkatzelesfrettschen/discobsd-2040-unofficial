"""Extract exact fork, image-swap, scheduler and SwapRAM definitions for host gates."""

import argparse
from pathlib import Path

from swap_ioctl_extract import extract_function


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    fork = (args.root / "sys/kern/kern_fork.c").read_text()
    swap = (args.root / "sys/kern/vm_swap.c").read_text()
    ram = (args.root / "sys/arch/rp2040/rp2040/swapram.c").read_text()
    text = (args.root / "sys/kern/exec_aout.c").read_text()
    sched_source = (args.root / "sys/kern/vm_sched.c").read_text()
    # The allocator is compiled separately against the same kernel headers.
    # Codec and I/O boundaries are supplied by the fixture; fork and image
    # ownership definitions remain verbatim, including their preprocessor arms.
    swap = swap[swap.index("/*\n * The clean text") :]
    fork = fork[fork.index("int     mpid;") :]
    ram_state = ram[ram.index("#define SR_DATA") : ram.index("static int              sr_spools;")]
    offset_start = ram.index("#if SWAPRAM_KB * 1024 <= 65535")
    offset_end = ram.index("} sr_tab[NPROC];", offset_start) + len("} sr_tab[NPROC];")
    ram_state += ram[offset_start:offset_end]
    ram_state += "\nstatic int sr_admit, sr_nlarge, swapramdebug;\n"
    ram_state += "static unsigned int sr_codec_owner;\n"
    ram_functions = "\n".join(
        extract_function(ram, signature, signature)
        for signature in (
            "static unsigned int\nsr_compressed_before (",
            "static unsigned int\nsr_compressed_size (",
            "static void\nsr_pool_init_once (",
            "static struct sr_ent *\nsr_slot(",
            "int\nswapram_reserve (",
            "void\nswapram_put (",
            "void\nswapram_commit (",
            "void\nswapram_cancel (",
            "int\nswapram_present (",
            "void\nswapram_in (",
            "size_t\nuser_top (",
        )
    )
    init = (args.root / "sys/kern/init_main.c").read_text()
    boot = init[
        init.index("fork_result = newproc(0);") : init.index("/* Child process with pid 1:")
    ]
    boot = "static void fork_swap_boot(void) { int fork_result; " + boot + "}\n"
    inherit = extract_function(ram, "void\nswapram_inherit (", "inherit")
    release = extract_function(text, "void\nexec_text_release(", "text release")
    # The fixture keeps its own sched() for the boot path, which only needs to
    # stop the boot parent, so the production scheduler loop is renamed.
    sched = extract_function(sched_source, "void\nsched()", "scheduler loop")
    sched = sched.replace("void\nsched()", "void\nkernel_sched()", 1)
    args.output.write_text(
        ram_state
        + "\n"
        + ram_functions
        + "\n"
        + swap
        + "\n"
        + sched
        + "\n"
        + fork
        + "\n"
        + inherit
        + "\n"
        + release
        + "\n"
        + boot
    )


if __name__ == "__main__":
    main()
