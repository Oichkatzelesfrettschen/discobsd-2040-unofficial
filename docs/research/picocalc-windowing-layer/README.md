# Windowing-layer study: inputs of the size measurements

measure.sh rebuilds the sizes quoted in ../picocalc-windowing-layer.md. It clones
nothing, takes the donor checkouts from the environment, and writes only under
the directory given as its one argument.

    MW_DIR=<microwindows> UGUI_DIR=<UGUI> LVGL_DIR=<lvgl> sh measure.sh <outdir>

Donor revisions measured:

- Microwindows (ghaerr/microwindows): 36c8d0edfa5423600027438b8f4bf0aa5bb49d28
- uGUI (achimdoebler/UGUI): ce0bccb5b7d4877c42081419fccadf7aa5727303
- LVGL (lvgl/lvgl): 622f63df2f7da0c063eb26e6d1fa2702822f6193

Compiler: arm-none-eabi-gcc (Arch Repository) 16.2.0, `-Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
-ffunction-sections -fdata-sections`; linking uses `-nostdlib -nostartfiles
--gc-sections -lgcc`.

Files here: scr_stub.c (4-bpp shadow screen driver), main_stub.c (ten-call
engine client), main_nx.c (Nano-X client), sizes.c (struct sizes, not run by
the script), main_lv.c (LVGL label and button with a 10-row partial buffer),
purpose.c (the own-layer core prototype), system.h (uGUI type stub). The LVGL
configuration is derived from the checkout's lv_conf_template.h by an awk
filter inside the script. Libc, GdError, kbddev, mousedev, mwstdpal8 and the
unused fblinear tables are linked as absolute dummy symbols
(`-Wl,--defsym=<name>=0x20000001`), so each figure covers the library code
only; the images are not runnable. No Microwindows file is patched.

Output of one clean run (arm-none-eabi-size, Berkeley columns):

    FIG mw_engine_closure text=25100 data=12560 bss=2460
    FIG mw_nanox_wm_floor text=43512 data=12940 bss=2700
    FIG mw_nanox_wm_unpatched text=74352 data=13340 bss=2708
    FIG mw_nanox_wm_all_objects text=89374 data=13348 bss=2734
    FIG own_core_prototype text=1232 data=8 bss=51396
    FIG ugui_whole_library text=11906 data=0 bss=12
    FIG lvgl_label_button text=104116 data=0 bss=34684

The study quoted 25,078 B for the engine closure (a sum of text and rodata
input sections from the link map; size reports 25,100), and 43,520 and 74,360
B text for the two Nano-X links (link order changes padding by 8 B).
