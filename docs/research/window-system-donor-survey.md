# Window-system donor survey: clipping, damage, drivers, hand-off and input in prior systems

Finding. Eleven window and console systems that run on bitmap displays were read as source for the mechanisms the PicoCalc windowing layer needs (docs/research/picocalc-windowing-layer.md). Their answers agree on five points: devices receive boxes while clipping lives above them; the visible region is recomputed on a stacking change and the newly exposed part is the repaint set; damage collapses to one box when a list grows; a graphics owner that dies leaves the text console restored only where the kernel does it; and keyboard-only operation rests on one focus owner with per-control mnemonics. The survey is reading, not measurement: nothing was compiled or run. Where a donor's source is absent (the X server core), the section says so and names what is present instead.

## 1. Scope and evidence class

- Method: eleven mining reports (one per slice of the donor trees) and two citation checks, all by source reading with bounded find, grep and sed. Nothing was built, run or booted. A behavior in a donor is therefore "described from source", never observed.
- Sample checks. The first check cited 48 claims from four reports and reached 46 confirmed, 2 shifted and 0 wrong; its two corrections are applied below (a clipped-glyph routine in the HP ddx starts at tcGlyph.c:237, not 217; struct wsscreen is defined in NetBSD wsdisplay.c:94, not in the header). The second check cited 56 claims from seven reports and reached 54 confirmed, 1 shifted and 1 wrong, plus seven behavior verdicts, all confirmed (two with qualifiers recorded in the sections that use them). The wrong item (a member list for the UnixWare save-under window record) is replaced below; the shifted one (the SunView ancestor-opaque removal) is cited at its found lines. A confirmed verdict means the cited line holds the named symbol within five lines; it does not vouch for the behavior described around it.
- Not sample-checked: every claim outside those samples. The sections mark the larger groups (the NetBSD and OpenBSD USL-compatibility handshake, the DragonFly lines, the RetroBSD gpanel dispatch, most FreeBSD vt line numbers other than the ones the checks cover, the NetBSD vcons helper functions other than vcons_redraw_screen and vcons_do_switch).
- Absent from every tree read: the X11R5 and X11R6 server core (dix/ and mi/). What exists is headers, per-screen hook tables, the SCO server glue, a function-order profile of the linked SCO server (Xsco.order, with no legend) and the client-side Xlib region code. Statements about X server clipping, exposure and backing store therefore rest on headers, call sites and function names, and are labeled so.
- Search limits: the NetBSD and OpenBSD searches for region or clip code covered wscons, rasops and wsfb only, and the FreeBSD slice did not search for clipping or backing store, so "not found" for those donors is a limited negative, not a verified absence.

## 2. Donor table

Revision: no donor tree's revision was recorded. Where a report quotes a per-file version line it is given as read; a per-file line does not identify the tree. Paths in section 3 are relative to the named donor's root; three long UnixWare prefixes are abbreviated here once: display kit = usr/src/i386at/ihvkit/display; SCO server = usr/src/i386/X11R6/programs/Xserver/hw/sco; XFree86 server = usr/src/i386/X11R6/programs/Xserver/hw/xfree86.

| Donor | What is present | License lines as the reports quote them | What it contributes |
|---|---|---|---|
| The SunOS 4.1.3 source | SunView: sys/sunwindowdev (windt.c, wintree.c, winioctl.c), usr.lib/libsunwindow (rect, win, pw, misc), sys/sun/cons.c, sys/sun/fbio.h, sys/sundev (cgsix.c, bwtwo.c, kbd.c); per-file SCCS 1.1 92/07/30 | "Copyright (c) 1987 by Sun Microsystems, Inc." (cons.c, windt.c); "Copyright (c) 1984 by Sun Microsystems, Inc." (rectlist.c); "Copyright 1986-1989 Sun Microsystems, Inc." (pf.c); "(C) Copyright 1983 by Sun Microsystems, Inc." (kbd.h) | Rectangle lists, visible-region and damage protocol, retained windows, console redirect, shared event queue |
| The SunOS 4.1.4 source | usr.lib/libpixrect (pixrect.h, cg6_ops.c); per-file SCCS 1.1 94/10/31; libsunwindow and windt.c are the same files up to the SCCS date for the files compared | "Copyright (c) 1984 by Sun Microsystems, Inc." family | Device operation table (pixrectops) |
| The Solaris 2.6 source | uts/i86pc/io/kd/kd (kdvt.c, kdstr.c) | "Copyright (c) 1990, 1991 UNIX System Laboratories, Inc." | SVR4 VT_PROCESS hand-off |
| The Schillix source (OpenSolaris-derived) | usr/src/uts/common/io (tem.c, kbtrans_streams.c), usr/src/cmd/vt/vtdaemon.c | "CDDL HEADER START" | Terminal emulator mode switch names; read at function-name level only |
| The UnixWare 7 source | Display kit headers and the lfb256 sample driver (SI 1.0 and 1.1); SCO server hw/sco (mit, nfb headers, ports/xxx template, ports/gen); XFree86 server console and VT code; Xsco.order; uslclients dtm and olwsm; X11R5 client libraries | "Copyright (c) 1991 1992 1993 USL" with MIT and DEC permission text (display kit headers); "Copyright (c) 1990, 1991, 1992, 1993 UNIX System Laboratories, Inc." (lfb256); "Copyright (C) The Santa Cruz Operation, 1991-1993" and "1991-1997" (SCO server); "Copyright (c) 1993 Intel Corporation" (lfbCompat.c); Thomas Roell 1990,91, David Wexelblat 1993, David McCullough 1993 (XFree86 files) | SI driver interface, window-op and GC-op tables, hand-off sequences, software key repeat, desktop keyboard use |
| 4.4BSD-Lite2 contrib | X11R5-hp300 hpbsd ddx (topcat, hyperion, cfb, mfb, HIL input), X11R5-luna68k sol ddx, X11R5-lib/lib/X XRegion.c and region.h | "Copyright (c) 1986, 1987 by Hewlett-Packard Company" with a MIT permission block (hpScreens.c); no license text in solKbd.c ("remade by A.Fujita, DEC-16-1992"); "Copyright 1987, 1988 by Digital Equipment Corporation" (XRegion.c) | Client region code, HP and Luna ddx hand-off bodies |
| NetBSD | sys/dev: wscons (wsdisplay.c v1.166, wsdisplay_vcons.c, wsconsio.h v1.130, wsevent.c, wsmux.c, wskbd.c, wsdisplay_compat_usl.c), rasops, wsfont, wsfb/genfb.c (v1.92), ic/ssdfb.c | "Copyright (c) 1996, 1997 Christopher G. Demetriou" (wsdisplay.c); "Copyright (c) 1999 The NetBSD Foundation, Inc." (rasops); "Copyright (c) 2005, 2006 Michael Lorenz" with BSD-2 text (vcons); "Copyright (c) 2019 The NetBSD Foundation, Inc." (ssdfb) | Cell shadow, mode switch and close, OLED dirty box, event ABI, font registry |
| OpenBSD | sys/dev: wscons (wsdisplay.c v1.156, 2026/04/17), rasops, wsfont, fdt/ssdfb.c | "Copyright (c) 1996, 1997 Christopher G. Demetriou"; "Copyright (c) 2018 Patrick Wildt" with ISC-style text (ssdfb.c) | Per-cell immediate flush, burner hook in the mode switch |
| FreeBSD | sys/dev/vt (vt_core.c, vt.h, vt_buf.c, vt_font.c, hw/fb/vt_fb.c), sys/sys (consio.h, kbio.h, font.h), sys/dev/evdev/input.h, sys/dev/kbd/kbd.c | "SPDX-License-Identifier: BSD-2-Clause" (vt); "Copyright (c) 1999 Kazutaka YOKOTA" (kbd.c); "Copyright (c) 2016 Oleksandr Tymoshenko" (input.h) | vt dirty rectangle, VT_PROCESS hand-off with dead-owner timer, keyboard modes |
| DragonFly | sys/dev/misc/syscons/syscons.c | "Copyright (c) 1992-1998 Soren Schmidt" | VT_PROCESS and K_RAW cases only (named, little read) |
| RetroBSD | sys/include/gpanel.h, sys/pic32/gpanel.c, src/libgpanel | "Copyright (C) 2015 Serge Vakulenko" with a permissive "without fee" text; LICENSE "Copyright (c) 2014, RetroBSD" | Closest prior art: a command-per-ioctl panel device |

Donors read but not mined: Microwindows, pixman-region, uGUI, LVGL (see the design note); the Solaris 2.8 headers, the SunOS man pages, XView and OSF/Motif trees (present, not read).

## 3. Mechanisms

### 3a. Clipping representations

X band-list regions.
- Client copy, 4.4BSD-Lite2 contrib X11R5-lib/lib/X: region.h:31 defines BoxRec; XRegion.c (1635 lines) implements miRegionOp (772), miCoalesce (621), miIntersectO (351), miUnionO (1112) and miSubtractO (1311), under the public XIntersectRegion (414), XUnionRegion (1177), XSubtractRegion (1458) and XXorRegion (1485). Its header comment (XRegion.c:28-48) describes a "y-x-banded" array of rectangles in which every rectangle of a band spans the band's full height, rectangles of a band do not touch horizontally, and adjacent bands with identical rectangles merge. The file is the client's copy, not the server's miregion.c.
- Server types, UnixWare display kit headers: BoxRec is four shorts x1 y1 x2 y2 (include/miscstruct.h:45-48). RegDataRec is a size and a numRects followed in memory by the BoxRec array (include/regionstr.h:45-49); RegionRec is an extents BoxRec plus a RegDataPtr (regionstr.h:51-54); a null data pointer means one rectangle, the extents (macros REGION_NIL, REGION_NUM_RECTS, REGION_RECTS, REGION_BOX, REGION_END, REGION_SZOF at regionstr.h:56-65). The header does not state the banded order.
- Server operations: ScreenRec carries RegionCreate (include/scrnintstr.h:179), RegionInit (180), Intersect (184), Union (185), Subtract (186), Inverse (187), RegionReset (188), TranslateRegion (189), RectIn (190), RegionExtents (194), RegionValidate (196), BitmapToRegion (197) and RectsToRegion (198). Their bodies are not in the tree. Xsco.order names the linked functions miRegionOp (line 1886), miUnionO (5054), miIntersectO (3011), miSubtractO (1676) and miCoalesce (1423); the profile lists seven miregion.c functions (adding miAppendNonO and QuickSortRects). miInverse was searched for and not found.
- Users in the HP ddx: topcat/tcPolyFSR.c:42-53 and topcat/tcPutImage.c:109-111 read regions through REGION_RECTS and REGION_NUM_RECTS; cfb/cfbsolid.c:238 calls miClipSpans. The definitions live in the missing mi library.

SunView rectlists (the SunOS 4.1.3 source).
- Types: struct rect {r_left, r_top, r_width, r_height} (usr.lib/libsunwindow/include/sunwindow/rect.h:46-49); struct rectnode {rn_next, rn_rect} (rectlist.h:23-26); struct rectlist {rl_x, rl_y (an offset), rl_head, rl_tail, rl_bound} (rectlist.h:30-37), a singly linked list plus a bounding rectangle, with offset macros at rectlist.h:47-59. Node allocation was not read.
- Operations in usr.lib/libsunwindow/rect/rectlist.c, compiled for user and kernel (726 lines, #ifndef KERNEL guards): rl_intersection (67-90) intersects every rectangle of rl1 with all of rl2 and appends the results; rl_union (139-150) copies and calls _rl_union, with no coalescing; rl_difference (152-170) copies rl1, subtracts each rectangle of rl2 with _rl_removerect and rebuilds the bound with _rl_makebound; rl_sort (93-136, user side only) is a selection sort by rect_order; rl_coalesce (373-399) collapses the list to its bound when the summed rectangle area equals the bound's area.

Comparison with this port's planned list: the design note plans a bounded list with rectangle subtraction (eight windows need no band-list; docs/research/picocalc-windowing-layer.md section 3). SunView's list is the same shape (rectangles, subtract, intersect, bound) without a count bound. X's band form buys a canonical order at the cost of the coalesce and band-merge passes of XRegion.c.

What this port takes:
- A flat list of exact rectangles with intersect and difference, and the bound kept beside it, as struct rectlist does (design inference).
- The rl_coalesce test, one area comparison against the bound, as the cheap collapse when a list grows past its bound (design inference).
- Boxes, not regions, below the layer (3e).

What does not fit:
- An unbounded linked list: the window has a fixed 192 KiB, so a bound and an overflow rule are required (3c).
- rl_union without coalescing grows a list on every union.
- X's band-list functions and the server's region API: the server bodies are absent here, and the design note names pixman-region.c as the source to read if band lists are wanted.

### 3b. Visible-region computation on a stacking change

SunView (the SunOS 4.1.3 source).
- sys/sunwindowdev/windt.c:1122-1145 dtop_computeclipping: if dt_parentinvalidwin is set it calls wt_setclipping on that window with dt_rlinvalid, sets focus and cursor, clears dt_rlinvalid and, when notification is on, queues notifications for the windows. windt.c:1147-1175 dtop_invalidateclipping converts a damaged rectangle to root-relative coordinates through the ancestors, unions it into dt_rlinvalid and calls dtop_computeclipping unless the shared data lock is held.
- sys/sunwindowdev/wintree.c:241 wt_setclipping. From the first report: (1) each window's w_rlexposed starts as its rectangle intersected with its parent's (the root: the whole screen); (2) ancestors' opaque area is removed with rl_intersection against the parent's w_rlexposed (the check found this at wintree.c:297-299; wintree.c:255-264 is the rl_copy, rl_free and root initialization) and each covering sibling on the WL_COVERING chain is subtracted; (3) the fixup rlfix is the affected rectangles intersected with the initial clip, minus the previous exposure w_rlexposedold unless WF_DTOPPOSCHANGED is set; (4) the walk recurses into children (WL_BOTTOMCHILD chain, wintree.c:331-333), subtracts covering children (339-341) and calls rl_coalesce (343). The result per window is w_rlexposed, the visible area, and w_rlfixup, the area to repaint. The check confirmed that w_rlexposed is built across wintree.c:255-343 and that the fixup is built at 359 and stored at 403-404; the first report places the rlfix computation at 349 (section 4).

X server (headers and the function-order profile only).
- ScreenRec hooks ValidateTree (include/scrnintstr.h:125), PostValidateTree (126) and CopyWindow (130). WindowRec holds clipList (include/windowstr.h:91, the output clip), borderClip (92, clipped-by-children area plus border), valdata (93, validation scratch), winSize (94) and borderSize (95). ValidateRec (include/validate.h:36-48) has a before part (oldAbsCorner, borderVisible, resized) and an after part (exposed, borderExposed); VTKind (validate.h:34) names VTOther, VTStack, VTMove, VTUnmap and VTMap.
- The SCO server installs miMarkWindow (scoScreen.c:177), miMarkOverlappedWindows (178), miMoveWindow (181), miSlideAndSizeWindow (182) and HandleExposures = miHandleValidateExposures (184) in sco/mit/scoScreen.c.
- Xsco.order names mivaltree.c@miComputeClips (line 2829), window.c@ReflectStackChange (12433) and WhereDoIGoInTheStack (10330), miRecomputeExposures (4037) and miCheckSubSaveUnder (2753). No body is in the tree. The profile's numbers beside miComputeClips have no legend (section 4); no size is claimed.

What this port takes: a window's visible area is its rectangle, intersected with the screen, minus every window above it (the SunView formulation), recomputed on a stacking, move or resize change, with the repaint set kept as new visible minus old visible (SunView's w_rlfixup; X's ValidateRec exposed, as far as headers show). For eight windows a flat z-order loop does this without a tree.

What does not fit: SunView keeps the window tree and its clipping in the kernel under a shared-data lock, and recurses over children; X splits clipList from borderClip for child clipping and borders. The port has neither a kernel window tree nor child windows.

### 3c. Damage and repair protocols

SunView, the SunOS 4.1.3 source (usr.lib/libsunwindow unless noted).
- Kernel side, sys/sunwindowdev/winioctl.c: WINGETDAMAGEDRL returns w_rlfixup (case near line 850); WINGETEXPOSEDRL returns w_rlexposed; WINDONEDAMAGED (case at 872-877) frees w_rlfixup and clears WF_PREVIOUSDAMAGED when the client's clip id matches.
- Client side, pw/pw_access.c: pw_exposed (359-368) fetches the exposed clipping and records the clip id as damagedid; pw_damaged (370-388) fetches the damaged clipping through WINGETDAMAGEDRL; pw_donedamaged (390-401) sends WINDONEDAMAGED with damagedid, then reinitializes the clipping and calls pw_exposed.
- Resize and repaint, win/win_central.c: pw_change is registered for SIGWINCH (185); the resize path (759-795) calls win_send_repaint per window (781). win_send_repaint (855-960) allows REDO_LIMIT 3 (858) passes; per pixwin it calls pw_damaged, repairs from the retained image when there is one (901-917), notifies the client with WIN_REPAINT if damage remains (959-960) and calls pw_donedamaged (935).
- Screen lock and clip id: pwco_lockstd (pw/pw_access.c:406-480) bumps pwcd_lockcount and sends WINLOCKSCREEN with the rectangle (about 462-470); the kernel (winioctl.c:310-321) runs dtop_lockdisplay and returns a new clip id; if the client's pwcd_clipid differs, the client refetches the clipping. pwco_unlockstd (481-about 500) sends WINUNLOCKSCREEN (493-494). The first report reads the lock as protection against other drawers and the window manager and the clip id as detection that clipping changed while locked. pw_shared_lock, the shared-memory fast path, was not read.

X (headers and names only): ScreenRec WindowExposures (include/scrnintstr.h:127), PaintWindowBackground (128), PaintWindowBorder (129), CopyWindow (130), ClearToBackground (131) and ClipNotify (132). miHandleExposures, miWindowExposures and miPaintWindow have no located definition.

FreeBSD vt (sys/dev/vt).
- vb_dirtyrect in vt.h:220 (comment at 196-200). vtbuf_dirty (vt_buf.c:267-278) grows one bounding box; vtbuf_dirty_cell (281-288) dirties a cell; vtbuf_make_undirty (292-297) and vtbuf_undirty (300-305) copy the box out and reset it.
- vt_flush (vt_core.c:1481) calls vtbuf_undirty at 1548, then invalidates and blits at 1557-1589; vt_flush_to_buffer (1365) and vt_bitblt_buffer (1405) serve the path that unlocks first. The mouse cursor dirties its cell (vt_core.c:1320-1328).

NetBSD vcons (sys/dev/wscons, cell shadow).
- struct vcons_screen (wsdisplay_vconsvar.h:44-96): scr_chars (uint32_t *), scr_attrs (long *), scr_status (VCONS_IS_VISIBLE), scr_busy and scr_flags (VCONS_DONT_DRAW, VCONS_NO_REDRAW and a dirty counter under VCONS_DRAW_INTR); macros SCREEN_IS_VISIBLE, SCREEN_BUSY and SCREEN_IDLE, SCREEN_CAN_DRAW (98-106). vcons_alloc_buffers (wsdisplay_vcons.c:311-339) makes one allocation with scr_chars after scr_attrs.
- Draw only if changed: vcons_putchar_buffer (wsdisplay_vcons.c:1277-1296) updates the shadow cell and returns whether it changed; vcons_putchar (1321-1346) calls it first and draws through the real putchar only when the screen is visible and drawable and the cell differs or holds the cursor. The copy and erase wrappers (copycols 996-1016, erasecols 1098-1115, copyrows 1152-1171, eraserows 1255-1273) move the shadow, then draw only for a visible, drawable screen. These helper lines were not sample-checked.
- vcons_redraw_screen (wsdisplay_vcons.c:664-763): take the screen lock and return unless the screen is visible and drawable; erase the screen if RI_FULLCLEAR is set; walk every shadow cell from scr_current_offset, skipping blanks that match the current run; draw each non-blank cell and flush runs of equal-attribute blanks as one erasecols call; clear RI_CURSOR, redraw the cursor and unlock. vcons_do_switch (592-661) marks the old screen invisible, waits while it is busy, makes the new screen visible and calls vcons_redraw_screen unless VCONS_NO_REDRAW is set.

NetBSD ssdfb (sys/dev/ic/ssdfb.c, an OLED with no mappable frame store).
- A shadow of the controller's memory sc_gddram (ssdfbvar.h:357-358). ssdfb_damage (ssdfb.c:1107-1130) sets sc_modified in non-poll mode and signals a condition variable; ssdfb_thread (1196-1236, created by kthread_create at 407 at PRI_SOFTCLOCK) waits with cv_timedwait, timeout 0 in emulator mode and sc_backoff otherwise (1213-1215); sc_backoff starts at 1 (292), doubles to mstohz(200) on idle timeouts (1216-1218) and resets to 1 when work is found (1221).
- ssdfb_sync_ssd1306 (definition at 1252-1304) transposes each 8x8 block of the rasops bitmap, compares it with the same block of sc_gddram, updates changed blocks and grows one bounding box x1..y2 in block units, then sends one rectangle with one sc_transfer_rect call. The ssd1322 (1308-1382) and ssd1353 (1385-1433) variants follow the same compare-and-grow pattern. sc_transfer_rect is supplied by the bus code and was not read. The first report states that each rasops operation calls ssdfb_damage and that this is the only damage notification path (not sample-checked).

OpenBSD ssdfb (sys/dev/fdt/ssdfb.c, 843 lines).
- No kernel thread, task, tsleep, timeout or callout (the check searched for each). ssdfb_putchar (746-757) calls the rasops putchar and then ssdfb_partial for that one cell; ssdfb_partial (476-513) builds a one-bit-per-pixel buffer for the rectangle rounded to 8-row pages and sends it with ssdfb_set_range (449-473) and ssdfb_write_data. Init flushes the whole screen once (442) and the clear path flushes it again (648-657). Line 673 is a UDLIO_DAMAGE ioctl, a flush a program requests.

What this port takes:
- One bounded dirty list whose overflow collapses to one bounding box: FreeBSD vt and NetBSD ssdfb both keep exactly one box; SunView keeps rectangle lists and collapses with rl_coalesce.
- Draw only what changed, by comparing against retained state before drawing (vcons_putchar_buffer; the ssdfb block compare), at widget or cell granularity.
- SunView's order of repair: retained image first, client repaint only for what remains damaged (3d).

What does not fit:
- A comparison against a shadow of the panel's pixels (sc_gddram): the shadow options cost 12,800-51,200 B in the design note, section 1.
- A flush thread with condition-variable backoff (ssdfb_thread); the layer runs in one user process and flushes from its own loop (design inference).
- Per-cell immediate flush (OpenBSD): each flush pays the panel's window setup of 11 bytes, 7 us (design note, section Method), which a batched box avoids.
- The SunView screen lock and clip id: they arbitrate several drawing processes, and the port has one drawer (rule 12 of the reconciled section in the design note).

### 3d. Backing store, retained windows and save-unders

SunView retained windows (the SunOS 4.1.3 source, usr.lib/libsunwindow).
- win/win_central.c:580-603 pw_set_retain allocates pw_prretained with mem_create(width, height, depth), depth 1 for a two-color colormap; callers at 468, 567 and 570; win_register calls win_set_retain at 173-174 (definition not read). The images are freed on unregister (212-213, 249-253).
- Drawing writes through to the retained pixrect when pw_prretained is set (pw/pw_rop.c:148-161, pw_line.c:78, pw_copy.c:44-47); pw_get reads from it (pw_get.c:32).
- pw/pw_access.c:768-787 pw_repairretained preloads the left column with XOR, then calls pw_repair on the clip bound to copy bits to the screen. win_send_repaint uses it (win_central.c:911) when PW_RETAIN is set and the window was not resized, or has a fixed image. pw_dontclipflag is explained at pw_rop.c:33-44.

X backing store and save-under (headers and names).
- WindowRec bits backingStore:2 (include/windowstr.h:107), saveUnder:1 (108) and DIXsaveUnder:1 (109); backStorage (102, null when disabled); ScreenRec backingStoreSupport and saveUnderSupport (include/scrnintstr.h:82).
- ScreenRec hooks SaveDoomedAreas (141), RestoreAreas (142), ExposeCopy (143), TranslateBackingStore (145), ClearBackingStore (146) and DrawGuarantee (147).
- The SCO server sets ChangeSaveUnder = miChangeSaveUnder and PostChangeSaveUnder = miPostChangeSaveUnder (sco/mit/scoScreen.c:179-180).
- mibstore.c inventory from the function-order profile (names only; bodies not read, so when it saves and what it keeps are not established): 23 functions, among them miBSAllocate, miBSSaveDoomedAreas, miBSRestoreAreas, miBSGetImage, miBSCopyArea, miBSPolyFillRect, miBSDestroyWindow, miBSFree, miCreateBSPixmap, miDestroyBSPixmap, miBSValidateGC, miBSCreateGCPrivate, miBSChangeWindowAttributes and miBSCloseScreen. The names suggest a per-window pixmap and GC wrapping (inference).
- USL save-under structures (include/sisave.h): SUPrivRec (44-49); SUPrivScrn, which holds the pSUPriv member (56); SUWindowRec (60-67) with pWin, box (62, the initial geometry), pCache (64), pImageClip (65) and next; cache types SU_LOCALCACHE 0 and SU_SDDCACHE 1 (37-38); globals SUWindows (69) and SURegion (70); SUSupport tests saveUnderSupport != NotUseful (82); macros SUCheckDrawable (103), SUCheckRegion (109, which calls the screen's RectIn) and SUTestNoExpose (137, no expose to a window inside another's pImageClip); a note on restore at unmap (128). No save or restore body exists in the tree; the only save-under files are the header and the two windowstr and scrnintstr lines.
- HP ddx: cfb/hpbstore.c (125 lines) holds hpSaveAreas, called from miSaveAreas per its header comment, and mfb/mfbbstore.c (137 lines) is the mono counterpart. hpsprite.c:542-565 hpSpritePaintWindowBackground tests pScreen->RectIn against the saved region and then calls PaintWindowBackground.

What this port takes:
- Expose-by-repaint from widget state as the default, with an optional per-window retained buffer, repaired before any client repaint: the SunView order.
- A retained buffer at the lowest depth the content needs: SunView uses depth 1 for two-color colormaps; the design note's canvas buffer is 4 bits.

What does not fit:
- Server-owned automatic backing store for every window: the X implementation is absent here, and a per-window pixmap does not fit one 192 KiB window next to the application.
- Save-under as a separate mechanism: menus and dialogs repaint from retained widget state and need no saved pixels (design inference; save-under in X avoids client exposes that this layer does not send).

### 3e. Device interface between device-independent code and hardware

SunOS pixrectops (the SunOS 4.1.4 source, usr.lib/libpixrect/pixrect/pixrect.h; the pr/pixrect.h copy is identical by cmp; the 4.1.3 tree has no pixrect.h by the search made).
- struct pixrectops at pixrect.h:110-133: pro_rop (111); user-only (#ifndef KERNEL, 112-121): pro_stencil, pro_batchrop, pro_nop, pro_destroy, pro_get, pro_put, pro_vector, pro_region; pro_putcolormap (122); user-only pro_getcolormap (123-125); pro_putattributes (126); user-only pro_getattributes (127-129); a KERNEL-only pro_nop (130-132). The kernel build omits the user-only members and adds pro_nop in their place.
- cg6_ops.c:25 and sys/sundev/cgsix.c:264 (cg6_ops) fill the table; sys/sunwindowdev/windt.c:216 uses mem_ops for the cursor pixrect. libcore cgdd.c:23 (struct cgddvars) and :94 (_core_cgdd) are the older pixwindd path.

SCO nfb window ops and GC ops (SCO server nfb headers).
- nfbWinOps (nfb/nfbWinStr.h:60-112): CopyRect (61), DrawSolidRects (63), DrawImage (66), DrawMonoImage (69), DrawOpaqueMonoImage (73), ReadImage (77), DrawPoints (79), TileRects (82), DrawMonoGlyphs and DrawFontText (86-108, two variants chosen by TBIRD_TEXT_INTERFACE), reserved WinOp11-13 (109-111), SetClipRegions (112, a Box array, a count and a drawable) and ValidateWindowGC (113). Every operation takes boxes or points plus alu, planemask and a DrawablePtr; none takes a RegionPtr.
- nfbWindowPriv holds ops, saveops and revops (131-135); revops holds DrawFontTextR (122-128). nfbGCOps (nfb/nfbGCStr.h:50-61): FillRects (51), FillSpans (53), FillZeroSegs (55), FillPolygons (57), GCOp5 and GCOp6 (59-60). The GC private keeps pCompositeClip, a RegionPtr (nfbGCStr.h:92), so regions exist at the GC level and boxes at the window-op level. That the core clips before calling the window ops is inferred from these signatures: the nfb implementation files are absent.
- nfbScrnPriv (nfb/nfbScrStr.h:108-158) holds SetColor, LoadColormap, ClearFont, InitRootWindow, BlankScreen, ValidateWindowPriv, clip_count, nfb_version, nfb_options, SaveGState and RestoreGState.

The two-primitive template driver (SCO server ports/xxx and ports/gen).
- ports/xxx/xxxData.c:88-104 binds CopyRect, DrawSolidRects, DrawMonoImage, DrawOpaqueMonoImage, DrawPoints, TileRects, DrawMonoGlyphs and DrawFontText to gen functions; only DrawImage (91) and ReadImage (94) are specific to the template, and their bodies are empty (xxxImage.c:39-57 and 69-90; xxxReadImage computes width and height and prints only under DEBUG_PRINT). xxxValidateWindowGC is also template-specific but is not a draw operation.
- ports/gen/genRectOps.c:36-98 genCopyRect loops one row at a time over ReadImage then DrawImage, choosing the direction from the overlap (74-84); genDrawSolidRects (145-201) builds one scanline of the foreground (165-187) and calls DrawImage per row (194-195); genTileRects (207 on) does GXcopy tiling with DrawImage and DrawSolidRects (232-233); genFillRct.c:21-36 forwards to DrawSolidRects; genGlyph.c:33-50 loops DrawMonoImage per glyph. genMono.c:16 says its mono routines use DrawSolidRects (bodies past line 60 not read). Unfilled slots point to genStubs.c:86-104, which call FatalError.
- There is no capability flag per operation: whatever the driver does not override keeps a gen function (xxxData.c:88-104). The entry points the template shows: ReadImage, DrawImage, SetColor, BlankScreen, ValidateWindowPriv, ValidateWindowGC and the scoScreenInfo set (SetGraphics, SetText, SaveGState, RestoreGState, CloseScreen) in xxxSysInfo (xxxData.c:113-124, with exposeScreen, isConsole and runSwitched true). The template's SaveGState and RestoreGState are empty and BlankScreen returns TRUE without acting (xxxScreen.c:38-47, 76-95).

USL SI 1.1 (display kit include/sidep.h).
- SIFunctions selects SI_1_1_Functions (sidep.h:430); the 1.0 struct spans 441-774 and the 1.1 struct 781-1131. Mandatory 1.1 members: si_init (785), si_restore (792), si_vt_save (794), si_vt_restore (796), si_vb_onoff (798), si_initcache (802), si_flushcache (804), si_download_state (806), si_get_state (812), si_select_state (818), si_screen with SCREEN_ENTER and SCREEN_LEAVE (822; 247-248); scanline access si_getsl, si_setsl, si_freesl (830-839); colormap (844, 851); hardware cursor (861-874). Optional members: spans (884), bit blits ss, ms and sm (893, 902, 912), stipple blits (925, 936, 948), polygons including si_poly_fillrect (963-980), points (989), lines (997-1021), arcs (1031-1057), fonts including si_font_stplblt (1069-1091), cache (1103-1116), extension (1123) and keyboard events (1128).
- Capabilities live in SIFlags (sidep.h:380-414): each SIavail_* field has the function in bits 0-7, options in 8-15 and general bits in 16-31 (113-119); SSBITBLT_AVAIL 1, MSBITBLT_AVAIL 2 and SMBITBLT_AVAIL 4 (1312-1314); fill bits CONVEXPOLY, GENERALPOLY and RECTANGLE (1344-1346); cursor types CURSOR_NONE, CURSOR_TRUEHDWR and CURSOR_FAKEHDWR (1422-1424). "Optional" means the server tests a bit or a null pointer; the only such test in the tree is in the macros and the dispatch code is absent.
- The graphics state passed to the device carries its clip list: SIGState (224-243) with SGcliplist, SGclipCNT and SGclipextent (240-242); SIbitmap (158-166) and SICursor (249-257) complete the data types.
- The lfb256 sample driver (display kit lfb256): lfbInitLFB sets the bit-blit bits to 1|2|4, SIavail_point to PLOTPOINT, tiles and stipples to 32x32 and SIstatecnt to 8 (lfbInit.c:48-57); lfbGlob.c:87 binds the screen-to-screen blit to lfbSSbitblt (lfbBitblt.c:137); fill, spans, stipple, font and cache entries return SI_FAIL (lfbPoly.c:42-47; lfbSpans.c:20-26; lfbStplblt.c, lfbFont.c, lfbCache.c); the ATI vendor files override the fill and blit (devices/ati/atiPoly.c:17, registered at atiInit.c:287 and 290) and were not read in full.

FreeBSD vt_driver (sys/dev/vt).
- vt.h:338-356 typedefs vd_blank, vd_bitblt_text, vd_invalidate_text, vd_bitblt_bmp, vd_bitblt_argb, vd_drawrect, vd_setpixel, vd_suspend and vd_resume; struct vt_driver (vt.h:360 on): vd_probe, vd_init, vd_fini, vd_blank, vd_drawrect, vd_setpixel, vd_bitblt_text, vd_invalidate_text, vd_bitblt_bmp, vd_bitblt_argb, vd_fb_ioctl, vd_fb_mmap, vd_postswitch, vd_suspend, vd_resume, vd_priority and vd_bitblt_after_vtbuf_unlock. Example table: hw/fb/vt_fb.c:45-58; other drivers (efifb, vt_vga, ofwfb, simplefb, vbefb) sit under sys/dev/vt/hw.

NetBSD wscons and rasops.
- struct wsdisplay_emulops (wsdisplayvar.h:54 on): cursor, mapchar, putchar, copycols, erasecols, copyrows, eraserows, allocattr. struct wsdisplay_accessops (123-130): ioctl, mmap, alloc_screen, free_screen, show_screen, load_font, pollc, scroll. struct rasops_info (rasops/rasops.h:79 on): ri_bits, ri_hwbits (shadow support), ri_font, ri_wsfcookie, ri_hw, ri_crow, ri_ccol, pixel-format fields and ri_real_ops (158, used to intercept putchar for rotation); per-depth initializers rasops1_init through rasops32_init (199-205). The per-depth putchar and copycols bodies were not read.

RetroBSD gpanel (sys/include/gpanel.h).
- Nine ioctls, all _IOW('g', n, struct), at gpanel.h:94-102: GPANEL_CLEAR (1; color, xsize, ysize), PIXEL (2), LINE (3), RECT (4), FILL (5), CIRCLE (6), IMAGE (7; x, y, width, height, a const unsigned short pointer), CHAR (8; font, color, background, x, y, sym) and TEXT (9; UTF-8 text). gpanel_font_t is at gpanel.h:9-22. Dispatch is sys/pic32/gpanel.c:459 gpanel_ioctl, cases at 465, 479, 489, 499, 509, 519, 529, 540 and 552 (not sample-checked). The header has no read, select or query ioctl. Triangle fill exists only in the library (src/libgpanel/fill_triangle.c; gpanel.h:140).

Comparison with /dev/lcdgfx (sys/sys/lcdgfx.h commands, lines 288-308; "none" means no member in the cited table):

| Capability | lcdgfx | gpanel | pixrectops | nfbWinOps | SI 1.1 |
|---|---|---|---|---|---|
| pixel | LCDG_PIXEL (289) | GPANEL_PIXEL | pro_put, pro_get | DrawPoints | si_plot_points |
| solid rectangle | LCDG_FILL (288), LCDG_FRAME (291) | GPANEL_FILL, GPANEL_RECT, GPANEL_CLEAR | not read | DrawSolidRects | si_poly_fillrect |
| line | LCDG_LINE (290) | GPANEL_LINE | pro_vector | none | si_line_onebitline, _seg, _rect |
| circle, arc | LCDG_CIRCLE (292) | GPANEL_CIRCLE | none | none | si_drawarc, si_fillarc |
| screen-to-screen copy | none | none | pro_rop | CopyRect | si_ss_bitblt |
| image from memory | LCDG_WINDOW plus write (RGB565, INDEX, BITS; 293) | GPANEL_IMAGE | pro_rop from a memory pixrect | DrawImage | si_ms_bitblt |
| read back | LCDG_READ, superuser, 20 pixels a read (303) | none | pro_get | ReadImage | si_sm_bitblt |
| text or glyphs | none (the host sends a BITS window) | GPANEL_CHAR, GPANEL_TEXT | none | DrawMonoGlyphs, DrawFontText | si_font_download, si_font_stplblt |
| palette | LCDG_PALETTE (294) | none | pro_putcolormap, pro_getcolormap | SetColor in nfbScrnPriv | si_set_colormap, si_get_colormap |
| scroll | LCDG_SCRDEF, LCDG_SCROLL (296, 295) | none | none | none | none |
| sprites, tiles | LCDG_SPRITE, LCDG_TILESET, LCDG_TILEWIN (304, 299, 300) | none | none | none | none |
| capability or size query | LCDG_INFO, LCDG_XINFO (298, 307); ENOTTY on a kernel without LCD_GFX_EXT (lcdgfx.h:96-97) | none | pro_getattributes | nfb_options | SIFlags avail bits |
| vertical sync | LCDG_WAITLINE (305) | none | none | none | none |
| ownership | open, last close (lcdgfx.h:11-17), LCDG_POWER (306) | none | none | SetGraphics, SetText | si_vt_save, si_vt_restore |

| Capability | FreeBSD vt_driver | NetBSD wscons and rasops |
|---|---|---|
| rectangle fill | vd_drawrect | erasecols, eraserows (cell units) |
| copy | none in the table (vd_bitblt_* draw text, bitmaps and ARGB) | copycols, copyrows (cell units) |
| text | vd_bitblt_text | putchar |
| bitmap | vd_bitblt_bmp, vd_bitblt_argb | none |
| pixel | vd_setpixel | none |
| mode and ownership | vd_postswitch, vd_suspend, vd_resume | show_screen, ioctl (WSDISPLAYIO_SMODE) |

What this port takes:
- The interface is the ioctl set. The layer keeps every LCDG_* call in one small device shim (fill, window write, flush) so a host model panel can stand in for it, the way the nfb template confines device code to a few entry points (design inference).
- The lcdgfx list is closest to gpanel (commands as ioctls, one panel) and has no copy, no glyph and no state commands. Capability is discovered by LCDG_INFO and ENOTTY, not a bit table.
- SI's design shows clip lists crossing the device boundary in the graphics state; here the layer clips above the device and sends boxes.

What does not fit:
- nfb's gen fallbacks build copy from ReadImage and DrawImage per row (genRectOps.c:36-98). For this panel that is a per-pixel read at 8.6-12.5 us (design note), root only; the layer repaints from state instead.
- SI and nfb function tables with dozens of optional slots, filled per driver: there is one device.
- vt_driver and wsdisplay ops serve a character grid; the layer's cell console already exists in the kernel.

### 3f. Console and graphics-owner hand-off

Reading of this section: every donor below is described from source. Where a restore happens in user space, it runs only if the owner is alive to run it; the port's restore is a kernel last-close path (comparison at the end of the section).

The SunOS 4.1.3 source.
- sys/sun/cons.c:103-104: TIOCCONS resets the console device; cons.c:122 redirects console I/O to a device after a permission check; cons.c:157 undoes the redirect. The file header (lines 9-18) describes the console as the primary frame buffer and keyboard.
- Framebuffer ownership: struct cg6_softc has an owner (sys/sundev/cgsix.c:120-122); FBIOSATTR (769-800) changes it through dev_specific FB_ATTR_CG6_SETOWNER_CMD and _PID, and ONLY_OWNER_CAN_SATTR (777-784) is compiled out, so the owner is set by pid and not by a switch. sys/sun/fbio.h defines struct fbtype (16), FBIOGTYPE (25), fbgattr and FBIOGATTR (78, 88), cg6_info and FBIOGXINFO (244, 275) and FBIO_ASSIGNWID (298).
- Not found: a restore-on-exit or crash path in the SunOS 4.1.3 kernel console; cons.c only redirects.

Solaris x86 kd, the Solaris 2.6 source (uts/i86pc/io/kd/kd).
- kdvt.c: VT_SETMODE (476); VT_RELDISP (489-518) with release and acquire acknowledgement and a timeout through ch_timeid; VT_ACTIVATE (525); VT_WAITACTIVE (537); kdvt_rel_refuse (607); kdvt_setmode_bottom (645); text state save and restore around KD_TEXT0 (327-339). kdstr.c:619-620 and 958-959 discard console writes while the console is in KD_GRAPHICS.
- The Schillix source: tem.c names tem_kdsetmode (727), tem_set_fbmode (945), tem_activate (971), tem_switch (991) and tems_modechange_callback (613); vtdaemon.c sets signals at 141, 203 and 445 and calls VT_ACTIVATE at 365, 703 and 805. Function names only.

The UnixWare 7 source, SCO server (sco/mit). Release in scoScrSw.c, in order (the ev_* and VT_RELDISP calls sit in #if !defined(usl) blocks):
- scoReleaseScreen (499): SaveScreen(ON) 520; pointer sprite off 526; scoSaveScreen cover window if exposeScreen 530; SaveGState 532; SetText 535; grab 546-553; ev_flush 557; screenActive = FALSE 561; KDGETLED 564; VT_RELDISP VT_TRUE 567.
- scoAcquireScreen (571): ev_resume 591; VT_RELDISP VT_ACKACQ 595; SetGraphics 611; RestoreGState 613; scoRestoreScreen 616; SaveScreen(OFF) 619; pointer on 621; ungrab 631; KDGETLED 642 to reapply the LEDs.
- Entry and handlers: scoEnterVtProcMode (415) sets VT_SETMODE VT_PROCESS, retried up to five times with a one-second nap (425-440) and gives up on failure; the signal handlers sco_rel_screen (743) and sco_acq_screen (748) only set flags, and the acquire handler also calls ev_resume; scoLeaveVtProcMode (463) restores default handlers and VT_AUTO (484-490); scoBecomeActiveScreen (701) loops VT_RELDISP VT_ACKACQ with sleep(2); scoWaitUntilScreenActive (715) waits on the acquire flag; the usl build adds scoVTSwitch (843), scoVTSwitchAway (876, VT_RELDISP 1) and scoVTSwitchTo (885, VT_ACKACQ). SIG_REL is SIGUSR1 and SIG_ACQ is SIGUSR2 (217-218).
- Open (scoInit.c): VT_OPENQRY (465); CONS_GET saves defaultMode (494); VT_GETMODE (497); VT_PROCESS with relsig and acqsig both SIGUSR2 (500-505); KDSETMODE KD_GRAPHICS (507); scoVTRequest (302) sets switchReqPending; the SIGUSR2 handler is installed in InitOutput at 713, inside the usl block (712-714); scoRestoreVid (890) restores the saved mode with MODESWITCH.
- Exit: ddxGiveUp (scoIo.c:653-684). The non-usl branch (663-674) closes the event queue if open, calls scoRestoreKbd (669) and scoRestoreVid (670) when the screen is active and then scoLeaveVtProcMode (674); the usl branch (677-684) does KDQUEMODE NULL (677), MODESWITCH to defaultMode (678), KD_TEXT0 (679) and VT_AUTO (681-684); it unlinks the /dev/X server files (690-692). AbortDDX (scoIo.c:711) calls scoSetText (713, which calls SetText on each active screen; scoScreen.c:363) and then ddxGiveUp (714). scoRestoreKbd (scoKbd.c:1005) leaves scancode mode with sc_exit and KDSKBMODE K_XLATE (1014-1017) and restores the tty mode (1020-1022).
- Not found: a SIGSEGV or SIGTERM handler in scoInit.c; scoAbortServerN is declared (scoInit.c:258) and called (670) with no definition under hw/sco. A crash reaches the restore only through AbortDDX, not confirmed for every path.

XFree86 (the UnixWare 7 source, XFree86 server).
- Leave: xf86VTSwitch (common/xf86Events.c:1267-1300) calls EnterLeaveVT(LEAVE) per screen, disables keyboard and pointer (1283-1285), then xf86VTSwitchAway (os-support/shared/VTsw_usl.c:56-62, VT_RELDISP 1). Enter: xf86VTSwitchTo (VTsw_usl.c:69-76, VT_RELDISP VT_ACKACQ), EnterLeaveVT(ENTER), SaveScreens and enabling the devices (xf86Events.c:1308-1320); xf86VTSwitchPending (VTsw_usl.c:51) is polled at xf86Events.c:1241.
- Open (os-support/sysv/sysv_init.c): VT_OPENQRY (104), VT_ACTIVATE (139), VT_WAITACTIVE (143), VT_PROCESS (154-157), KD_GRAPHICS (161).
- Close: sco xf86CloseConsole (os-support/sco/sco_init.c:192-210) issues VT_RELDISP, MODESWITCH to the saved mode, VT_AUTO and close, and no KDSETMODE; the sysv version (sysv_init.c:191, KD_TEXT at 199) and the Solaris x86 version (solx86_init.c:235-259) do issue KD_TEXT. ddxGiveUp (common/xf86Init.c:385-395) calls xf86CloseConsole and then abort() if a signal was caught; AbortDDX is at 408. No explicit save or restore of fonts or palette was found in the files read; the EnterLeaveVT driver hooks were not read.

HP and Luna ddx (4.4BSD-Lite2 contrib).
- hpbsd (X11R5-hp300/mit/server/ddx/hpbsd): init.c:722-724 opens the grf device, issues GRFIOCGINFO and GRFIOCON; init.c:752-760 maps the registers with GRFIOCMAP; failure paths issue GRFIOCOFF (init.c:754, topcat/topcat.c:297, hyperion/hyperion.c:114). AbortDDX (init.c:841) and ddxGiveUp (init.c:847) have empty bodies, as the check confirmed. No atexit, signal or sigaction handler was found in hpbsd. hyperion.c:205-212 sets SaveScreen to hyperSaveScreen and CloseScreen to hyperCloseScreen.
- Luna sol (X11R5-luna68k/mit/server/ddx/sol): solInit.c:157-170 defines solSetConsoleMode and solResetConsoleMode with only a DEBUG-guarded fprintf in the body; solInit.c:175 AbortDDX and 189 ddxGiveUp call the reset, so the console restore is a no-op. The framebuffer is opened and mapped in solBm.c:19-25.

FreeBSD vt (sys/dev/vt, sys/sys/consio.h).
- Constants: KD_TEXT and KD_GRAPHICS (consio.h:49-52); VT_AUTO and VT_PROCESS (326-327); VT_SETMODE (337); VT_FALSE, VT_TRUE and VT_ACKACQ (340-342); VT_RELDISP (344); VT_ACTIVATE (347); VT_WAITACTIVE (350).
- VT_SETMODE (vt_core.c:3054-3100, case label 3050) returns EPERM when another process owns the vt; VT_PROCESS records vw_proc and vw_pid and validates relsig and acqsig (EINVAL). VT_RELDISP (3105-3140) is accepted only for the current vt in VT_PROCESS and only from its owner; VT_TRUE completes a release and VT_ACKACQ an acquire.
- vt_proc_window_switch (496-560) signals the owner; vt_window_preswitch (444-456) arms a dead-owner timer of hz * vt_deadtimer, default 15 s (133), with callback vt_switch_timer (346). vt_proc_alive (2216-2231) downgrades the window to VT_AUTO and clears vw_pid; the switch then proceeds (547-551, "Dead. Cleaning"). KDSETMODE (2914-2930) sets VWF_GRAPHICS (vt.h:312); KDGETMODE is at 2745-2747.
- No code was found that resets KD_GRAPHICS to KD_TEXT on owner death; the flag is per window and only KD_TEXT clears it. On a switch, vt_window_switch (566-645) sets VDF_INVALID and calls vd_postswitch (vt_fb.c:483-491, which calls the framebuffer's enter hook), and the redraw comes from the vtbuf contents. That this restores the panel in practice was not tested. DragonFly syscons.c:1220-1273 has the same VT_PROCESS logic (not sample-checked).

NetBSD and OpenBSD wscons.
- NetBSD: SCR_GRAPHICS 4 and SCR_DUMBFB 8 (wsdisplay.c:103-104); WSDISPLAYIO_GMODE (1347-1349) and WSDISPLAYIO_SMODE (1356-1376), which accepts EMUL, MAPPED and DUMBFB, clears SCR_GRAPHICS, sets it again for MAPPED and DUMBFB and forwards to the driver; EMUL on a screen with no emulator returns EINVAL (1369-1370). wsconsio.h:433-437 gives GMODE _IOR('W', 75), SMODE _IOW('W', 76) and the modes 0, 1 and 2. OpenBSD has the same constants (wsconsio.h:532-534) and flags (wsdisplay.c:95-96).
- Close, NetBSD wsdisplayclose (wsdisplay.c:1072-1131): hangs up the tty and closes the line discipline (1093-1105); calls the emulator's reset and, when wsdisplay_clearonclose is set, its clear-screen (1110-1118); clears SCR_OPEN (1128). It clears SCR_GRAPHICS only in the emulator branch (1111). It makes no SMODE call and does not call vcons_redraw_screen (the check read 1072-1131 for both; a scr_syncops destroy call at 1108 was not read). wsdisplay_closescreen (498-522) hangs up and forces vnodes closed with vdevgone.
- genfb SMODE (wsfb/genfb.c:498-520): notifies the bus backend and sets sc_mode; when the new mode is EMUL it calls genfb_restore_palette and vcons_redraw_screen (515-518; redraw callers at 386, 517 and 532). vcons_redraw_screen has many other callers in the NetBSD tree (a grep listed arm, sparc, sgimips, hppa, amiga and macppc drivers); several other drivers handle SMODE as well.
- OpenBSD: wsdisplayclose (wsdisplay.c:902-959) clears SCR_GRAPHICS and resets the emulator (937-939), clears the screen when wsdisplay_clearonclose is set (940-942) and clears SCR_OPEN (952). SMODE (the case lies at about 1143-1172; the two reports differ) clears SCR_GRAPHICS, sets it for MAPPED and DUMBFB, sends WSEMUL_CLEARCURSOR and calls wsdisplay_burner_setup (2339-2357), which stops the screen burner while the screen is graphics and restarts it on return to text; it does not redraw text. wsdisplay_switch (1846-1917) has a SCR_GRAPHICS branch at 1907; the switch helpers were not read in full. OpenBSD has no vcons layer in this tree.
- Reading of the first report, not contradicted by the check: after an X server crashes, the close path clears the graphics flag and resets the emulator but does not itself issue SMODE EMUL or redraw from a shadow; text returns only if the graphics owner or a driver close hook sends SMODE EMUL, which reaches genfb's redraw. No driver close hook was found, and the search covered only the files read.
- USL compatibility (wsdisplay_compat_usl.c, not sample-checked): usl_sync_check_sig and an attach and detach handshake (NetBSD 142-196); a dead process (proc_find fails) triggers usl_sync_check (161-165); OpenBSD adds usl_detachtimeout and usl_attachtimeout (205-280). The switch chain is wsdisplay_switch then switch1, switch2 and switch3 with callbacks (NetBSD wsdisplay.c:2075-2132 and 1893-2013). The flow when an X server opens the device in mapped mode was not traced.

This port.
- Open of /dev/lcdgfx switches the panel from the text console to graphics; the console keeps updating its cell store but draws nothing; the last close, which process exit performs, returns the panel to the text configuration and repaints every cell; a second open fails with EBUSY (sys/sys/lcdgfx.h:11-17).
- lcdgfx_close (sys/arch/rp2040/dev/lcdgfx.c:294-308) ends an open window stream, returns the power mode to on under LCD_GFX_EXT, restores the panel-test setting under LCD_PANELTEST, clears gfx_open and returns lcdcons_gfx(0). lcdcons_gfx (sys/arch/rp2040/dev/lcdcons.c:663-678) takes the console's drawing flag, calls lcdterm_gfx(on) and flushes queued replies; its comment (655-661) says what the console deferred drains at the release into a terminal that is either silent or repainted, and that leaving cannot fail.
- Contrast: in the SCO server, XFree86, HP and Luna ddx, the restore is a user-space routine of the owner (ddxGiveUp, AbortDDX, xf86CloseConsole), so it runs only when the owner survives to call it, and two of those bodies are empty. FreeBSD handles a dead owner with a kernel timer and a vt_proc_alive downgrade and redraws from the vtbuf. NetBSD's close path resets the emulator and the redraw rests on the owner or driver sending SMODE EMUL. The port's repaint follows the descriptor's last close, which the kernel performs for a process that exits or is killed, with no signal handshake (an inference from lcdgfx.h:13 that process exit performs the last close, not exercised on a board).

What this port takes: the hand-off keeps the device owned by one open descriptor, and the text repaint stays in the kernel's last-close path, so a dead owner needs no timer; the inherited descriptor lets a child keep the pixels (design note, section 5). A signal-based release and acknowledge sequence (SCO, FreeBSD VT_PROCESS) is the model for a future task switch that must save and restore graphics state, and is not needed for the single-owner hand-off.

What does not fit: VT_PROCESS timers and acknowledgements arbitrate several virtual terminals; the port has one console and one graphics file. SunOS-style owner-by-pid changes (FBIOSATTR) assume several programs sharing a framebuffer.

### 3g. Input delivery

NetBSD and OpenBSD wscons (NetBSD sys/dev/wscons).
- struct wscons_event {u_int type; int value; struct timespec time} (wsconsio.h:56-60); WSEVENT_VERSION 1 (61); types WSCONS_EVENT_KEY_UP 1, KEY_DOWN 2 and ALL_KEYS_UP 3 (66-68).
- WSKBDIO_SETMODE _IOW('W', 19, int) (170) with WSKBD_TRANSLATED 0 and WSKBD_RAW 1 (172-173), handled at wskbd.c:1237; wsmux saves and restores the raw keyboard mode (wsmux.c:479).
- The queue is struct wseventvar with a circular array of struct wscons_event (wseventvar.h:81-89); wsevent_read (wsevent.c:229), wsevent_poll (283), wsevent_inject (401) and a kqueue filter (336). wsmux accepts WSMUXIO_ADD_DEVICE (wsmux.c:428); WSMUXIO_INJECTEVENT is at wsconsio.h:591.

FreeBSD (sys/sys/kbio.h, sys/dev).
- K_RAW 0, K_XLATE 1 and K_CODE 2 (kbio.h:13-15); KDSKBMODE accepts all three (vt_core.c:2766-2780); vw_kbdmode is per window (261, default K_XLATE) and is saved and restored across switches (353-374, 634-637); the raw read path passes non-XLATE codes through genkbd_putc (kbd/kbd.c:696-730). DragonFly syscons.c:1438 has a K_RAW case.
- evdev: struct input_event {struct timeval time; uint16_t type; uint16_t code; int32_t value} (evdev/input.h:42-47), EV_VERSION 0x010001 (49), EVIOCGRAB (108), event push at evdev.c:938.

The SunOS 4.1.3 source.
- sys/sundev/kbd.c: includes vuid_event.h (37); kbdd_ascii_addr (89); KIOCTRANS (464-470) switches between TR_ASCII and TR_EVENT; VUIDSFORMAT is near 455; KIOCSDIRECT is at 636 and KIOCGDIRECT (639-651) always returns 1.
- sys/sunwindowdev/windt.c: dt_sharedq is a shared input-event queue in a locked page (571-575); events are written to it at 2685-2699; the queue is shifted for dt_sharedq_owner at 812-813. usr.lib/libsunwindow/win/win_keymap.c defines the raw_code key table (35) and maps keys to input masks (271-358).

The UnixWare 7 source.
- SCO server (sco/mit): the console descriptor is the keyboard device (scoKbd.c:613-614). An event ring is enabled with KDQUEMODE and SIGUSR1 (scoEvents.c:243-254); the signal-driven XqueRequest handles XQ_MOTION, XQ_BUTTON and XQ_KEY; XQ_KEY goes through scoScancodeToKeycode to scoProcessKbdEvent (scoIo.c:614-621).
- scoKbdEnqueueEvent (scoKbd.c:719) maps a keysym to modifier state; Caps, Shift, Num, Scroll and Kana locks toggle lockState (796-955) and update the LEDs with KDSETLED (842-907). Key repeat is in software: a repeated make of the last key is checked against a per-key autoRepeats bitmap (773-792). Events enter the mi queue through mieqEnqueue (987); ProcessInputEvents (scoEvents.c:72-82) calls mieqProcessInputEvents and miPointerUpdate.
- XFree86: xf86KbdOn (os-support/shared/sysv_kbd.c:81-92) sets KDSKBMODE K_RAW, LDNMAP 0 and raw termios; xf86KbdOff (96-104) restores them. The sc_init that matches scoRestoreKbd's sc_exit was not found in these files.

HP and Luna (4.4BSD-Lite2 contrib).
- HIL: x_hilinit.c:744 opens the queue device /dev/rhil read-write and issues HILALLOCQ (747, 754); SetInputCheck watches the queue head (757); open_device (1260) issues HILMAPQ (1285, 1289). Luna: solKbd.c:35 opens /dev/kbd, KIOCMOUSE at 309 and 325; solDevGetEvents (88-130) reads a code buffer (read at 97) in which bytes 0x80-0x87 start a three-byte mouse packet and any other byte is a key code.

This port (sys/sys/lcdcons.h).
- A raw event session reports the keyboard controller's events: a state (LCD_KEY_PRESSED 1, LCD_KEY_HOLD 2, LCD_KEY_RELEASED 3; lines 84-86) and a key byte, in struct lcd_keyev (110-113). A held key reports again at the controller's repeat rate (lcdcons.h:38-39). The key byte is the ASCII character after Shift, Caps Lock and the symbol layer, or a controller code; Shift, Ctrl and Alt report as keys of their own (39-42). LCDIOC_KEYEVS starts and ends a session (122) and LCDIOC_GETKEY drains up to LCD_KEYEV_N, 6, events (121, 98) without blocking, with a lost count; the driver keeps LCD_KEYEV_QUEUE, 32, events (99) and drops the oldest when full (72-82).

What this port takes:
- Raw events carry release and repeat already, so the toolkit holds the modifier-down state from PRESSED and RELEASED events and needs no repeat timer: SCO repeats in software because its queue delivers make and break (scoKbd.c:773-792), while this controller reports repeat as further PRESSED or HOLD events.
- Lock state (Caps, Shift) is applied by the controller before the key byte reaches the toolkit (lcdcons.h:39-41), so the SCO lock table (scoKbd.c:796-955) is not needed; Ctrl and Alt combinations remain the toolkit's.
- A lost count of nonzero means rebuild key state from what follows (lcdcons.h:73-75), the same recovery wscons gives with ALL_KEYS_UP.

What does not fit:
- Blocking queue reads with poll or kqueue (wsevent_read, wsevent_poll): GETKEY never blocks, and whether select wakes on modifier-only events is unproven (design note, Not verified).
- Per-window keyboard mode (FreeBSD vw_kbdmode) and device multiplexing (wsmux): one console, one session owner.
- Pointer packets (HIL, Luna, XQ_MOTION): there is no pointer; Ctrl-arrows move a cursor in the design note.

### 3h. Fonts and glyph delivery

- SCO nfb: nfbGlyphInfo (nfb/nfbGlyph.h:22-27) is {BoxRec box, int stride, unsigned char *image, glyph_id}, one glyph's monochrome bitmap with its box and stride. genFont.c:87-120: when the driver's DrawMonoGlyphs is not the generic one, the code paints the background with DrawSolidRects (100-101), fills an array of glyph infos from ppBits[c] (108-113; glyph_id stores the bitmap pointer at 113) and makes one DrawMonoGlyphs call (120). genGlyph.c:33-50 is the generic version, a loop of DrawMonoImage per glyph; genFont.c:124-166 draws per character with a DrawSolidRects background when opaque.
- USL SI: SIGlyph (include/sidep.h:314-321) holds lbearing, rbearing, width, ascent, descent and the SFglyph bitmap; SIFontInfo (332-339) holds SFnumglyph, SFflag, ascent, descent, SFmin and SFmax, with flags SFTerminalFont 0x1, SFFixedWidthFont 0x2 and SFNoOverlap 0x4 (328-330). The per-screen download limit is SIFlags.SIfontcnt (407; no numeric cap found). si.h caches a hardware index and the font info per font (siPrivFont 45-58; siFontDims 17-24; UNOPT_FONT 1 and HDWR_FONT 4 at 36-37). Text is drawn by si_font_stplblt(fontnum, x, y, count, glyph index array, forcetype) (sidep.h:1091); the lfb sample's lfbStplbltFont (lfbFont.c:45) returns SI_FAIL, so it never downloads a font.
- FreeBSD: struct vt_font (sys/sys/font.h:74-82) holds vf_map[VFNT_MAPS], vf_bytes, vf_height, vf_width, vf_map_count[] and vf_refcount; vtfont_lookup (vt_font.c:83-111) does a binary search in the map and computes the glyph stride as howmany(width, 8) * height (110-111); vtfont_load is at 158-209 and the default font at vt_core.c:179-181.
- NetBSD and OpenBSD: a wsfont registry with compiled-in header fonts under sys/dev/wsfont (spleen, gallant and others); rasops_info carries ri_font and ri_wsfcookie. The per-depth putchar bodies were not read.
- SunOS 4.1.3: usr.lib/libsunwindow/misc/pf.c has pf_open (129), pf_open_private (184), pf_load_vfont (431, 435) and pf_build (552).
- HP ddx: tcImageVarGlyph (topcat/tcGlyph.c:237) is the clipped glyph path and falls back to miPolyGlyphBlt and miImageGlyphBlt (265-267, not in the tree); tcGlyph.c:86 documents the unclipped whole-glyph blit; hpRealizeFont is at hpFonts.c:75. Luna has no glyph code.
- RetroBSD: gpanel_font_t (gpanel.h:9-22) holds name, maxwidth, height, ascent, firstchar, size, 16-bit bits, offset, width (or 0 for a fixed font), defaultchar and bits_size; GPANEL_CHAR and GPANEL_TEXT draw with a font, color and background (8, 9).
- This port: the console font is 95 glyphs of 9 rows (lcdfont.h:LCDFONT_NCHARS 95, LCDFONT_HEIGHT 9, in a 6x10 cell), and the kernel's glyph bank takes 9 rows with the six cell columns in bits 7-2 (sys/sys/lcdcons.h:11-14).

What this port takes: a glyph is a box, a stride and a one-bit image delivered as a batch, the shape of nfbGlyphInfo and of LCDG_WIN_BITS (lcdgfx.h:36-38: one bit per pixel, rows padded to a byte, set bits in palette entry fg and clear bits in bg). An opaque text run can therefore go as one window with background and glyphs together, where nfb paints the background first (genFont.c:100-101); this is a design inference and unmeasured. Transparent text needs another path (sprites exist only with LCD_GFX_EXT, lcdgfx.h:96-97, 125-129).

What does not fit: SI font download and the hardware font index (the device has no glyph memory command); vt_font's multi-map lookup and wsfont's registry, which serve large Unicode sets; gpanel's device-side text, which the lcdgfx list does not have.

### 3i. Keyboard-only operation in the OPEN LOOK and Motif desktop clients

The UnixWare 7 source, usr/src/common/X11R5/uslclients/dtm (found by grep; not read in depth).
- olwsm/wsmproperty.c:522-526 defines mnemonic strings for apply, reset, factory, cancel and help; line 603 sets XmNmnemonic per button, 606 uses the operation DM_B_MNE_GET_FOCUS and 1037 calls DmRegisterMnemonicWithGrab. dm_cbs.c:349-357 builds real_mnemonic from the first character, only under REAL_MNEMONIC.
- Focus policy: olwsm/window.c:145 sets Mwm*keyboardFocusPolicy to explicit and :150 sets *pointerFocus to clickToType. Traversal moves focus by widget with XmProcessTraversal(..., XmTRAVERSE_CURRENT) at f_create.c:1643, is_util.c:2322 and wb_cbs.c:541.
- No accelerator table or mouseless mode was found in olwsm; olwsm/RootWindow.c:64 only inherits XtInheritDisplayAccelerator.

What this port takes: one explicit focus owner shown by a focus ring, traversal from widget to widget with Tab, and a mnemonic registered per button, so every control is reachable without a pointer.

What does not fit: window-manager focus policies (Mwm resources) and click-to-type; the port runs one application with its own stack.

## 4. Open contradictions and unknowns found in the donors

Each is stated as observed, not resolved.
- SCO release signal: scoScrSw.c:217-218 defines SIG_REL as SIGUSR1 and SIG_ACQ as SIGUSR2, and sets SIG_REL at 425 and 486, while scoInit.c:500-505 (501-502 in the check) sets relsig and acqsig both to SIGUSR2 and installs scoVTRequest as the SIGUSR2 handler at 713 inside the usl block. The two files disagree on the release signal; the files read do not settle which path runs in a given build.
- Xsco.order numbers: the map has no legend. The second number equals the count of offset lines not tagged low_usage in 1054 of 1054 entries, and the first equals the offset-line count in 749 of 1054; the meaning is otherwise unknown ("likely size and count"). A first report's reading of 151 as the byte size of miComputeClips is unsupported and is not used.
- Line of the rlfix computation in wt_setclipping: the first SunView report places it at wintree.c:349 and the check places the fixup build at 359 and its store at 403-404.
- OpenBSD SMODE case: one report gives wsdisplay.c:1143-1163 and another 1152-1172.
- SunOS trees: the 4.1.3 and 4.1.4 copies of rectlist.c and windt.c differ in the SCCS date, which is the only difference confirmed; a full diff was not run, and a statement that every key file differs was made without it.
- NetBSD close path: wsdisplayclose makes no SMODE or redraw call, no driver close hook that does so was found, and the scr_syncops destroy call at wsdisplay.c:1108 was not read; whether another path redraws is unconfirmed.
- FreeBSD: no code resets KD_GRAPHICS on owner death, and the redraw after a dead-owner switch comes from the vtbuf; whether it restores a panel was not tested.
- HP ddx: the first report listed AbortDDX and ddxGiveUp (init.c:841, 847) as unread; the check read them and found empty bodies. The later reading is used.
- UnixWare save-under: the first report listed pSUPriv in SUWindowRec; the check found it in SUPrivScrn (sisave.h:56). The corrected list is in 3d.
- nfb: the implementation files are absent, so whether the core clips before calling the window ops is inferred. SI: the dispatch that tests availability bits is absent.
- NetBSD and OpenBSD clipping, backing store and save-under: not found in the files searched, with the search limits stated in section 1.
- ssdfb: the claim that each rasops operation calls ssdfb_damage and that it is the only damage path is the first report's and was not sample-checked; sc_transfer_rect was not read.
- Solaris 2.6 kd: only kdvt.c and kdstr.c were read; kd.h, vt.h and kdvm.c were not.

## 5. Licenses

This survey is an engineer's reading of the license lines the reports quote, not legal advice.
- Reference for design only, no code and no line-for-line translation: the SunOS 4.1.3 and 4.1.4 sources (Sun Microsystems), the Solaris 2.6 source and the UnixWare 7 source (USL, SCO and Intel lines; the DEC and MIT permission text in the display kit headers applies to the upstream X code, and a clean upstream copy, not these trees, is the route to code under that text). These are proprietary and are treated as such here.
- CDDL: the Schillix tem.c, vtdaemon.c and kbtrans files (header "CDDL HEADER START"). CDDL is a file-level copyleft; code taken would remain CDDL files with source offered. The survey uses names only.
- MIT or X-style permission text: the HP hpbsd ddx (HP copyright with the MIT block) and the DEC copyright in XRegion.c (permission text not quoted by the report). Copyable with the notice kept, after the full text is read; the Luna sol files carry no license text and are reference only.
- BSD-family: FreeBSD vt (SPDX BSD-2-Clause), NetBSD wscons and rasops (BSD-style foundation terms; vcons BSD-2 text), OpenBSD ssdfb (ISC-style text), RetroBSD gpanel (permissive "without fee" text). Code taken keeps its notice. Not quoted in full by the reports: the OpenBSD and NetBSD wsdisplay.c terms (Demetriou), the DragonFly syscons terms and the FreeBSD kbd.c and evdev terms; read them before copying.
- This port's layer is written new. This tree's NOTICE states that every component is permissive (design note, section 3), and a new file here carries the project's own header; the survey's role is design reference, so no donor code enters the tree through it.
