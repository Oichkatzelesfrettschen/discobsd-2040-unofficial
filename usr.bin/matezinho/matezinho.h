/* Original MATEzinho code is under the repository's BSD 3-Clause license. */
#ifndef MATEZINHO_H
#define MATEZINHO_H
#include <stdint.h>
#define MZ_W 320
#define MZ_H 320
#define MZ_BAND 8
#define MZ_APPS 9
#define MZ_NOTES 1024
#define MZ_FILES 12
/*
 * The PicoCalc keyboard controller's two lights, as backlight.h in
 * usr.bin/backlight numbers them. The controller's firmware keeps the screen
 * level at a multiple of 16 within 16-240 and the keyboard level at a
 * multiple of 32 below 240, 0 meaning off (backlight.ino), so one Left or
 * Right press moves a light by one of those steps.
 */
#define MZ_SCREEN 0
#define MZ_KEYS 1
#define MZ_LIGHT_MAX 240
#define MZ_SCREEN_STEP 16
#define MZ_KEYS_STEP 32
struct mz {
    int focus, active, menu, chooser, large, note_dirty;
    unsigned open, dirty;
    unsigned char bands[40];
    unsigned char mru[MZ_APPS];
    unsigned ticks, asset_sectors;
    char note[MZ_NOTES], expr[64], result[64];
    char files[MZ_FILES][32];
    int nfiles, file_cursor;
    char location[128], status[48];
    /* light[]: the level the controller kept, -1 when it is unreadable.
     * light_want[]: the level Left or Right asked for, which the backend
     * writes, then sets back to -1 after reading light[] again. setting: the Settings
     * row, 0 screen, 1 keyboard, 2 large headings. */
    int light[2], light_want[2], setting;
};
struct mz_io {
    void *ctx;
    int (*icon)(void *, int, int, uint16_t *);
    int (*band)(void *, int, const uint16_t *);
};
extern const char *const mz_names[MZ_APPS];
void mz_init(struct mz *);
void mz_all(struct mz *);
void mz_action(struct mz *, int);
int mz_render(struct mz *, struct mz_io *);
/* Navigation actions are outside printable ASCII. Tab cycles retained views. */
enum { MZ_LEFT=256, MZ_RIGHT, MZ_UP, MZ_DOWN, MZ_HOME, MZ_MENU, MZ_CLOSE,
       MZ_SWITCH, MZ_TICK, MZ_SAVE, MZ_REFRESH, MZ_COMMIT };
#endif
