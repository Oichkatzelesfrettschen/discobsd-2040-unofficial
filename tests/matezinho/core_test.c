/* Original test code is under the repository's BSD 3-Clause license. */
#include "../../usr.bin/matezinho/matezinho.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Full frames belong only to this host test oracle, never the desktop. */
static uint16_t incremental[320*320], reference[320*320];
static int emitted;
/* Columns reported by every band callback, in MZ_XUNIT units. */
static long reported_units;
static int icon(void *ctx,int app,int row,uint16_t *pixels)
{
    int i;
    (void)ctx;
    assert(app>=0&&app<9&&row>=0&&row<24);
    for(i=0;i<24;i++)pixels[i]=(uint16_t)(app*100+row*24+i);
    return 0;
}
/* Only the reported columns reach the frame, as on the panel. */
static int sink(void *ctx,int y,const uint16_t *pixels,int xlo,int xhi)
{
    int j;
    assert(y>=0&&y<=312&&y%8==0);
    assert(xlo>=0&&xlo<xhi&&xhi<=MZ_COLS);
    for(j=0;j<8;j++)
        memcpy((uint16_t *)ctx+(y+j)*320+xlo*MZ_XUNIT,
            pixels+j*320+xlo*MZ_XUNIT,(size_t)(xhi-xlo)*MZ_XUNIT*2);
    reported_units+=xhi-xlo;
    emitted++;
    return 0;
}
/* The controller's firmware keeps what backlight.ino keeps; a light write
 * in the desktop's backend ends with this read-back. */
static void keep(struct mz *s)
{
    int v;
    if(s->light_want[MZ_SCREEN]>=0) {
        v=s->light_want[MZ_SCREEN]/16*16;
        s->light[MZ_SCREEN]=v<16?16:v;
    }
    if(s->light_want[MZ_KEYS]>=0) {
        v=s->light_want[MZ_KEYS]/32*32;
        s->light[MZ_KEYS]=v<32?0:v;
    }
    if(s->light_want[MZ_SCREEN]>=0 || s->light_want[MZ_KEYS]>=0) mz_all(s);
    s->light_want[MZ_SCREEN]=s->light_want[MZ_KEYS]=-1;
}
static void compare(struct mz *s)
{
    struct mz oracle=*s;
    struct mz_io a={incremental,icon,sink},b={reference,icon,sink};
    assert(mz_render(s,&a)==0);
    mz_all(&oracle);
    assert(mz_render(&oracle,&b)==0);
    if(memcmp(incremental,reference,sizeof(reference))) {
        int i;
        for(i=0;i<320*320;i++)if(incremental[i]!=reference[i])break;
        fprintf(stderr,"diff %d,%d focus=%d active=%d chooser=%d ticks=%u\n",i%320,i/320,s->focus,s->active,s->chooser,s->ticks);
        assert(0);
    }
}
int main(void)
{
    struct mz s;
    struct mz_io out={incremental,icon,sink};
    unsigned seed=0x1234567;
    int i;
    int actions[]={MZ_LEFT,MZ_RIGHT,MZ_UP,MZ_DOWN,MZ_HOME,MZ_MENU,MZ_CLOSE,
        MZ_SWITCH,MZ_COMMIT,MZ_TICK,9,10,8,'a',' ',127};
    mz_init(&s);
    compare(&s);
    assert(incremental[21*320]==0x9e6b);
    /* The six-column font packs "Files" into a 27-pixel tile label. */
    assert(incremental[73*320+42]==0x2124);
    assert(incremental[73*320+43]==0xef7d);
    assert(incremental[73*320+46]==0xef7d);
    assert(incremental[75*320+60]==0xef7d);
    assert(incremental[75*320+62]==0x2124);
    emitted=0;
    reported_units=0;
    mz_action(&s,MZ_RIGHT);
    assert(mz_render(&s,&out)==0);
    assert(emitted>0&&emitted<40);
    /* Moving the focus one tile dirties the two adjacent tile columns of the
     * bands they cross (pixels 8-208), never all 80 units. */
    assert(reported_units<(long)emitted*MZ_COLS);
    for(i=0;i<MZ_BANDS;i++) assert(s.xlo[i]==s.xhi[i]);
    compare(&s);
    mz_action(&s,10);
    assert(s.active==1);
    mz_action(&s,'N');mz_action(&s,'o');
    mz_action(&s,MZ_HOME);mz_action(&s,MZ_RIGHT);mz_action(&s,10);
    assert(s.active==2);
    mz_action(&s,MZ_SWITCH);assert(s.chooser);
    compare(&s);
    mz_action(&s,MZ_COMMIT);assert(s.active==1&&!s.chooser);
    assert(!strcmp(s.note,"No"));
    for(i=0;i<4000;i++)mz_action(&s,'x');
    assert(strlen(s.note)==MZ_NOTES-1);
    /* Settings: an unreadable light takes zero steps; steps from the kept or
     * the already requested level, clamped to 0-240; rows wrap. */
    mz_action(&s,MZ_HOME);
    while(s.focus!=4) mz_action(&s,MZ_RIGHT);
    mz_action(&s,10);
    assert(s.active==4&&s.setting==0);
    mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_SCREEN]==-1);
    compare(&s);
    s.light[MZ_SCREEN]=160; s.light[MZ_KEYS]=0; mz_all(&s);
    mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_SCREEN]==176);
    mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_SCREEN]==192&&s.light_want[MZ_KEYS]==-1);
    keep(&s);
    assert(s.light[MZ_SCREEN]==192);
    compare(&s);
    for(i=0;i<20;i++) mz_action(&s,MZ_LEFT);
    assert(s.light_want[MZ_SCREEN]==0);
    keep(&s);
    assert(s.light[MZ_SCREEN]==16);
    for(i=0;i<20;i++) mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_SCREEN]==240);
    keep(&s);
    mz_action(&s,MZ_DOWN);
    assert(s.setting==1);
    mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_KEYS]==32&&s.light_want[MZ_SCREEN]==-1);
    keep(&s);
    compare(&s);
    mz_action(&s,MZ_DOWN);
    assert(s.setting==2&&!s.large);
    mz_action(&s,10);
    assert(s.large);
    mz_action(&s,MZ_RIGHT);
    assert(s.light_want[MZ_SCREEN]==-1&&s.light_want[MZ_KEYS]==-1);
    compare(&s);
    mz_action(&s,MZ_DOWN);
    assert(s.setting==0);
    mz_action(&s,MZ_UP);
    assert(s.setting==2);
    mz_action(&s,' ');
    assert(!s.large);
    s.light[MZ_KEYS]=-1; mz_all(&s);
    compare(&s);
    s.light[MZ_KEYS]=64; mz_all(&s);
    for(i=0;i<1500;i++) {
        seed=seed*1664525u+1013904223u;
        mz_action(&s,actions[(seed>>16)%(sizeof(actions)/sizeof(actions[0]))]);
        keep(&s);
        assert(s.focus>=0&&s.focus<9&&s.active>=-1&&s.active<9);
        assert(s.setting>=0&&s.setting<3);
        assert(s.light[MZ_SCREEN]>=16&&s.light[MZ_SCREEN]<=MZ_LIGHT_MAX);
        assert(s.light[MZ_KEYS]>=0&&s.light[MZ_KEYS]<=224);
        assert(s.chooser>=0&&s.chooser<=9);
        compare(&s);
    }
    puts("matezinho core: navigation, retention, settings, bounds and incremental/full equality passed");
    return 0;
}
