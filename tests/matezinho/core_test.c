/* Original test code is under the repository's BSD 3-Clause license. */
#include "../../usr.bin/matezinho/matezinho.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Full frames belong only to this host test oracle, never the desktop. */
static uint16_t incremental[320*320], reference[320*320];
static int emitted;
static int icon(void *ctx,int app,int row,uint16_t *pixels)
{
    int i;
    (void)ctx;
    assert(app>=0&&app<9&&row>=0&&row<24);
    for(i=0;i<24;i++)pixels[i]=(uint16_t)(app*100+row*24+i);
    return 0;
}
static int sink(void *ctx,int y,const uint16_t *pixels)
{
    assert(y>=0&&y<=312&&y%8==0);
    memcpy((uint16_t *)ctx+y*320,pixels,5120);
    emitted++;
    return 0;
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
    emitted=0;
    mz_action(&s,MZ_RIGHT);
    assert(mz_render(&s,&out)==0);
    assert(emitted>0&&emitted<40);
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
    for(i=0;i<1500;i++) {
        seed=seed*1664525u+1013904223u;
        mz_action(&s,actions[(seed>>16)%(sizeof(actions)/sizeof(actions[0]))]);
        assert(s.focus>=0&&s.focus<9&&s.active>=-1&&s.active<9);
        assert(s.chooser>=0&&s.chooser<=9);
        compare(&s);
    }
    puts("matezinho core: navigation, retention, bounds and incremental/full equality passed");
    return 0;
}
