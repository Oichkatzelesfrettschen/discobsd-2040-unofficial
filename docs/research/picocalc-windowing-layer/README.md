# Windowing-layer study: inputs of the size measurements

measure.sh rebuilds the sizes quoted in ../picocalc-windowing-layer.md and prints
each as a FIG line (linked images, per-object sizes, the own-layer core) or a
STRUCT line (type sizes from sizes.c). It clones nothing, takes the donor
checkouts from the environment, and writes only under the directory given as its
one argument, which must not exist or must be empty. Each checkout must be the
root of a git work tree at the revision below with no modified tracked file; the
script stops before compiling otherwise. The three revisions are the values
MW_REV, UGUI_REV and LVGL_REV at the top of measure.sh.

    MW_DIR=<microwindows> UGUI_DIR=<UGUI> LVGL_DIR=<lvgl> sh measure.sh <outdir>

Donor revisions measured:

- Microwindows (ghaerr/microwindows): 36c8d0edfa5423600027438b8f4bf0aa5bb49d28
- uGUI (achimdoebler/UGUI): ce0bccb5b7d4877c42081419fccadf7aa5727303
- LVGL (lvgl/lvgl): 622f63df2f7da0c063eb26e6d1fa2702822f6193

Compiler: arm-none-eabi-gcc (Arch Repository) 16.2.0, `-Os -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft
-ffunction-sections -fdata-sections`; linking uses `-nostdlib -nostartfiles
--gc-sections -lgcc`.

Files here: scr_stub.c (4-bpp shadow screen driver), main_stub.c (ten-call
engine client), main_nx.c (Nano-X client), sizes.c (struct sizes, compiled by
the script), main_lv.c (LVGL label and button with a 10-row, 6,400 B RGB565 partial buffer),
purpose.c (the own-layer core prototype), system.h (uGUI type stub). The LVGL
configuration is derived from the checkout's lv_conf_template.h by an awk
filter inside the script. Libc, GdError, kbddev, mousedev, mwstdpal8 and the
unused fblinear tables are linked as absolute dummy symbols
(`-Wl,--defsym=<name>=0x20000001`), so each figure covers the library code
only; the images are not runnable. No Microwindows file is patched.

Output of one clean run (arm-none-eabi-size, Berkeley columns; nm -S for STRUCT):

    FIG mw_engine_closure text=25100 data=12560 bss=2460
    FIG mw_nanox_wm_floor text=43512 data=12940 bss=2700
    FIG mw_nx_obj_engine_devrgn text=3280 data=0 bss=0
    FIG mw_nx_obj_engine_devdraw text=2808 data=0 bss=0
    FIG mw_nx_obj_engine_devfont text=2631 data=0 bss=0
    FIG mw_nx_obj_drivers_fblin4 text=2248 data=80 bss=0
    FIG mw_nx_obj_engine_devclip text=768 data=0 bss=24
    FIG mw_nx_obj_drivers_genmem text=1006 data=0 bss=0
    FIG mw_nx_obj_drivers_rasterops text=9304 data=0 bss=0
    FIG mw_nx_obj_engine_convblit_frameb text=11716 data=0 bss=0
    FIG mw_nanox_wm_unpatched text=74352 data=13340 bss=2708
    FIG mw_nanox_wm_all_objects text=89374 data=13348 bss=2734
    STRUCT cliprect bytes=16
    STRUCT event bytes=44
    STRUCT evlist bytes=48
    STRUCT gc bytes=100
    STRUCT pixmap bytes=32
    STRUCT psd bytes=196
    STRUCT region bytes=32
    STRUCT window bytes=100
    FIG own_core_prototype text=1240 data=8 bss=51396
    FIG ugui_whole_library text=11906 data=0 bss=12
    FIG lvgl_label_button text=104116 data=0 bss=31484

The per-object FIG lines use the Nano-X flag set; rasterops.o is 9,304 B, of which
8,932 B is the function GdRasterOp. The note's Win32-layer sizes (26,372 B core,
28,279 B for ten controls) come from an earlier compile outside this script and
are not reproduced here. The study quoted 25,078 B for the engine closure (a sum
of text and rodata input sections from the link map; size reports 25,100), and 43,520 and 74,360
B text for the two Nano-X links (link order changes padding by 8 B).
