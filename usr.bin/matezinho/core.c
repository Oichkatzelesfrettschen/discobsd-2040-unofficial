/* Original MATEzinho code is under the repository's BSD 3-Clause license. */
#include "matezinho.h"
#include "../../sys/arch/rp2040/dev/lcdfont.h"
#include <stdio.h>
#include <string.h>

const char *const mz_names[MZ_APPS] = {
    "Files", "Notes", "System", "Calculator", "Settings", "Clock",
    "Console", "Help", "Storage"
};
static uint16_t pixels[MZ_W*MZ_BAND];
static int band_y;
static int clip_bottom=320;
#define BG 0x1082
#define SURFACE 0x2124
#define PANEL 0x18e3
#define TEXT 0xef7d
#define MUTED 0x9d13
#define GREEN 0x9e6b
#define SELECT 0x3186

/* Flag the bands of rows [y, y+h) and widen each one's dirty columns to cover
 * pixels [x, x+w), rounded outward to MZ_XUNIT units. */
static void
mark(struct mz *s, int x, int y, int w, int h)
{
    int i,lo,hi;
    if (x<0) { w+=x; x=0; }
    lo=x/MZ_XUNIT;
    hi=(x+w+MZ_XUNIT-1)/MZ_XUNIT;
    if (hi>MZ_COLS) hi=MZ_COLS;
    if (lo>=hi) return;
    for (i=y/8; i<MZ_BANDS && i<(y+h+7)/8; i++) {
        if (i<0) continue;
        if (s->xlo[i]>=s->xhi[i]) { s->xlo[i]=(unsigned char)lo; s->xhi[i]=(unsigned char)hi; }
        else {
            if (lo<s->xlo[i]) s->xlo[i]=(unsigned char)lo;
            if (hi>s->xhi[i]) s->xhi[i]=(unsigned char)hi;
        }
    }
    s->dirty=1;
}
void
mz_all(struct mz *s)
{
    int i;
    for (i=0; i<MZ_BANDS; i++) { s->xlo[i]=0; s->xhi[i]=MZ_COLS; }
    s->dirty=1;
}
void
mz_init(struct mz *s)
{
    int i;
    memset(s,0,sizeof(*s));
    s->active=-1;
    for(i=0;i<MZ_APPS;i++) s->mru[i]=(unsigned char)i;
    strcpy(s->location,"/");
    strcpy(s->status,"Ready");
    s->light[MZ_SCREEN]=s->light[MZ_KEYS]=-1;
    s->light_want[MZ_SCREEN]=s->light_want[MZ_KEYS]=-1;
    mz_all(s);
}
/*
 * Settings rows. Left and Right step a light from the level already asked
 * for, or else from the kept level, so several presses that reach the
 * backend together ask for their sum. The kernel takes 0-240, and the
 * controller rounds what it gets (MZ_SCREEN_STEP in matezinho.h).
 */
static void
settings(struct mz *s,int key)
{
    int row=s->setting,level,step;
    if(key==MZ_UP) s->setting=(row+2)%3;
    else if(key==MZ_DOWN) s->setting=(row+1)%3;
    else if(key==' ' || ((key=='\n'||key=='\r') && row==2)) s->large=!s->large;
    else if((key==MZ_LEFT || key==MZ_RIGHT) && row<2) {
        level=s->light_want[row]>=0?s->light_want[row]:s->light[row];
        if(level<0) return;
        step=row==MZ_SCREEN?MZ_SCREEN_STEP:MZ_KEYS_STEP;
        level+=key==MZ_RIGHT?step:-step;
        if(level<0) level=0;
        if(level>MZ_LIGHT_MAX) level=MZ_LIGHT_MAX;
        s->light_want[row]=level;
    } else return;
    mark(s,8,24,304,272);
}
static void
activate(struct mz *s, int a)
{
    int i,j;
    for(i=0;i<MZ_APPS;i++) if(s->mru[i]==a) break;
    for(j=i;j>0;j--) s->mru[j]=s->mru[j-1];
    s->mru[0]=(unsigned char)a;
    s->open|=1u<<a;
    s->active=a;
    s->menu=0;
    mz_all(s);
}
void
mz_action(struct mz *s, int key)
{
    int n;
    char *p;
    if(key==MZ_TICK) {
        s->ticks++;
        mark(s,272,0,48,24);
        if(s->active<0 || s->menu)mark(s,216,118,96,80);
        if(s->active==5 || s->active==2) mark(s,8,24,304,272);
        return;
    }
    if(key==MZ_HOME || key==27) {
        s->active=-1; s->menu=0; s->chooser=0; mz_all(s); return;
    }
    if(key==MZ_MENU) { s->menu=!s->menu; mz_all(s); return; }
    if(key==MZ_SWITCH) {
        for(n=s->chooser+1;n<=MZ_APPS;n++)
            if(s->open & (1u<<s->mru[n%MZ_APPS])) {
                s->chooser=n%MZ_APPS;
                if(s->chooser==0) s->chooser=MZ_APPS;
                mz_all(s); return;
            }
        return;
    }
    if(key==MZ_COMMIT && s->chooser) {
        n=s->mru[s->chooser%MZ_APPS];
        s->chooser=0; activate(s,n); return;
    }
    if(key=='\t') {
        for(n=1;n<=MZ_APPS;n++) {
            int a=(s->active+n+MZ_APPS)%MZ_APPS;
            if(s->open&(1u<<a)) { activate(s,a); return; }
        }
        return;
    }
    if(key==MZ_CLOSE && s->active>=0) {
        s->open&=~(1u<<s->active); s->active=-1; s->chooser=0; mz_all(s); return;
    }
    if(s->active<0 || s->menu) {
        n=s->focus;
        if(key==MZ_LEFT) s->focus=(n+8)%9;
        if(key==MZ_RIGHT) s->focus=(n+1)%9;
        if(key==MZ_UP) s->focus=(n+6)%9;
        if(key==MZ_DOWN) s->focus=(n+3)%9;
        if(key=='\n' || key=='\r') { activate(s,s->focus); return; }
        mark(s,8+(n%3)*104,24+(n/3)*88,96,88);
        mark(s,8+(s->focus%3)*104,24+(s->focus/3)*88,96,88);
        return;
    }
    if(s->active==4) { settings(s,key); return; }
    p=s->active==1?s->note:s->expr;
    if(s->active!=1 && s->active!=3) return;
    n=(int)strlen(p);
    if((key==8 || key==127) && n) p[n-1]=0;
    else if(s->active==1 && n<MZ_NOTES-1 && (key=='\n'||(key>=32&&key<127))) {
        p[n]=(char)key; p[n+1]=0;
    } else if(s->active==3 && n<63 && key>=32&&key<127) {
        p[n]=(char)key; p[n+1]=0;
    }
    if(s->active==1) s->note_dirty=1;
    mark(s,8,56,304,224);
}
static void
rect(int x,int y,int w,int h,uint16_t c)
{
    int i,j,x1=x+w,y1=y+h;
    if(x<0)x=0;
    if(y<band_y)y=band_y;
    if(x1>320)x1=320;
    if(y1>band_y+8)y1=band_y+8;
    if(y1>clip_bottom)y1=clip_bottom;
    for(j=y;j<y1;j++) for(i=x;i<x1;i++) pixels[(j-band_y)*320+i]=c;
}
static const unsigned char *
glyph_rows(unsigned char ch)
{
    if (ch < LCDFONT_FIRST || ch >= LCDFONT_FIRST + LCDFONT_NCHARS)
        ch = '?';
    return lcdfont[ch - LCDFONT_FIRST];
}

static void
glyph(int x, int y, const unsigned char *rows, uint16_t color, int scale)
{
    int row, col;

    for (row = 0; row < LCDFONT_HEIGHT; row++)
        for (col = 0; col < 6; col++)
            if (rows[row] & (128u >> col))
                rect(x + col * scale, y + row * scale, scale, scale, color);
}

static void
text(int x,int y,const char *p,uint16_t c,int scale)
{
    int start=x;
    unsigned ch;
    while((ch=(unsigned char)*p++)!=0) {
        if(ch=='\n') { x=start; y+=11*scale; continue; }
        if(x+6*scale>310) { x=start; y+=11*scale; }
        glyph(x, y, glyph_rows(ch), c, scale);
        x+=6*scale;
    }
}

static int
title_advance(const unsigned char *rows)
{
    unsigned ink = 0;
    int row, columns = 6;

    for (row = 0; row < LCDFONT_HEIGHT; row++)
        ink |= rows[row];
    while (columns > 1 && !(ink & (128u >> (columns - 1))))
        columns--;
    return columns + 1;
}

static void
tile_title(int x, int y, const char *title)
{
    const char *p;
    int advance = 0;

    if (y + LCDFONT_HEIGHT <= band_y || y >= band_y + MZ_BAND)
        return;
    for (p = title; *p; p++)
        advance += title_advance(glyph_rows(*p));
    /* The last glyph's advance includes the gap before the next glyph. */
    x += (96 - (advance - 1)) / 2;
    for (p = title; *p; p++) {
        const unsigned char *rows = glyph_rows(*p);
        glyph(x, y, rows, TEXT, 1);
        x += title_advance(rows);
    }
}

static const char *const setting_names[3] = {
    "Screen light", "Keyboard light", "Large headings"
};
/* A light's row: a bar of the kept level out of 240 and its number. */
static void
light_bar(const struct mz *s,int which,int y)
{
    char buf[12];
    int v=s->light[which];
    if(v<0) { text(112,y+2,"unavailable",MUTED,1); return; }
    rect(112,y,122,12,PANEL);
    rect(113,y+1,(v<MZ_LIGHT_MAX?v:MZ_LIGHT_MAX)*120/MZ_LIGHT_MAX,10,GREEN);
    if(which==MZ_KEYS && v==0) strcpy(buf,"off");
    else snprintf(buf,sizeof(buf),"%d",v);
    text(242,y+2,buf,TEXT,1);
}
static int
icon(struct mz_io *io,int app,int x,int y)
{
    uint16_t line[24];
    int j,i;
    for(j=0;j<24;j++) {
        if(y+j<band_y||y+j>=band_y+8) continue;
        if(io->icon(io->ctx,app,j,line)<0) return -1;
        for(i=0;i<24;i++) pixels[(y+j-band_y)*320+x+i]=line[i];
    }
    return 0;
}
static int
scene(struct mz *s,struct mz_io *io)
{
    int i,x,y,n=0,scale=s->large?2:1;
    char buf[80];
    rect(0,band_y,320,8,BG);
    rect(0,0,320,22,PANEL);
    rect(0,21,320,1,GREEN);
    text(8,6,"Applications   Places",TEXT,1);
    text(208,6,"MATEzinho",GREEN,1);
    snprintf(buf,sizeof(buf),"%02u:%02u",(s->ticks/60)%100,s->ticks%60);
    text(278,6,buf,MUTED,1);
    if(s->active<0 || s->menu) {
        for(i=0;i<9;i++) {
            x=8+(i%3)*104; y=30+(i/3)*88;
            rect(x,y,96,80,SURFACE);
            if(i==s->focus) {
                rect(x,y,96,2,GREEN); rect(x,y+78,96,2,GREEN);
                rect(x,y,2,80,GREEN); rect(x+94,y,2,80,GREEN);
            }
            if(icon(io,i,x+36,y+10)<0) return -1;
            tile_title(x, y + 42, mz_names[i]);
            if(i==1) text(x+12,y+59,s->note_dirty?"Unsaved":"Editor",MUTED,1);
            else if(i==8) text(x+12,y+59,"Asset volume",MUTED,1);
            else if(i==2) text(x+12,y+59,"192 KiB/user",MUTED,1);
            else if(i==5) {
                snprintf(buf,sizeof(buf),"%u:%02u",s->ticks/60,s->ticks%60);
                text(x+12,y+59,buf,MUTED,1);
            }
        }
    } else {
        rect(8,30,304,256,SURFACE);
        text(16,39,mz_names[s->active],GREEN,scale);
        switch(s->active) {
        case 0:
            text(16,66,s->location,MUTED,1);
            for(i=0;i<s->nfiles;i++) {
                if(i==s->file_cursor)rect(12,82+i*14,296,13,SELECT);
                text(16,84+i*14,s->files[i],TEXT,1);
            }
            break;
        case 1:
            clip_bottom=260;
            text(16,70,s->note,TEXT,1);
            clip_bottom=320;
            text(16,269,s->note_dirty?"Ctrl-S save | Unsaved":"Ctrl-S save",MUTED,1);
            break;
        case 2:
            text(16,76,"RP2040 profile\n264 KiB physical SRAM\n192 KiB process window\n320 x 320 RGB565\n8-row render buffer: 5120 B",TEXT,1);
            snprintf(buf,sizeof(buf),"Shell state: %u B",(unsigned)sizeof(*s));
            text(16,148,buf,MUTED,1); break;
        case 3:
            text(16,76,"Integer: a + b, -, *, /",MUTED,1);
            text(16,104,s->expr,TEXT,1);
            text(16,137,s->result,GREEN,1); break;
        case 4:
            for(i=0;i<3;i++) {
                y=68+i*22;
                if(i==s->setting) rect(12,y-4,296,20,SELECT);
                text(16,y+2,setting_names[i],TEXT,1);
                if(i<2) light_bar(s,i,y);
                else text(112,y+2,s->large?"on":"off",TEXT,1);
            }
            text(16,140,"Up/Down: choose  Left/Right: adjust\nSpace: large headings\nBoot level: root runs backlight -s",MUTED,1);
            text(16,186,"Original dark theme\nMATE artwork: CC BY-SA 3.0\nSee artwork AUTHORS and COPYING",TEXT,1); break;
        case 5:
            snprintf(buf,sizeof(buf),"Session: %u seconds",s->ticks);
            text(16,90,buf,TEXT,scale);
            text(16,155,"Wall clock is not configured",MUTED,1); break;
        case 6:
            text(16,80,"Enter: exclusive shell\nDisplay returns after shell exits.\nNo concurrent terminal promised.",TEXT,1); break;
        case 7:
            text(16,78,"Arrows: select  Enter: open\nEsc: desktop  Tab: next task\nCtrl-W: close  Ctrl-S: save\nF1: applications  F5: refresh\nSettings: lights and headings\nNotes: 1023 bytes, retained\nFiles: first 12 directory entries\nIcons: MATE/GNOME contributors\nCC BY-SA 3.0; upstream AUTHORS\nIndependent MATE-inspired shell",TEXT,1); break;
        case 8:
            snprintf(buf,sizeof(buf),"Asset capacity: %u MiB",s->asset_sectors/2048);
            text(16,64,buf,GREEN,1);
            text(16,78,"Read-only external asset pack\n1024-byte read cache\n16 MiB virtual ROM profile\n32 MiB development SD profile\n32 GB sparse SD profile\nStorage does not increase RAM.",TEXT,1); break;
        }
        text(16,284,s->status,MUTED,1);
    }
    rect(0,298,320,22,PANEL);
    rect(0,298,320,1,SELECT);
    text(6,305,"[]",GREEN,1);
    for(i=0;i<9;i++) if(s->open&(1u<<i)) {
        x=25+n*32;
        rect(x,301,30,17,i==s->active?SELECT:PANEL);
        if(i==s->active) rect(x,317,30,2,GREEN);
        memcpy(buf,mz_names[i],3); buf[3]=0;
        text(x+4,305,buf,TEXT,1); n++;
    }
    if(s->chooser) {
        rect(48,70,224,176,PANEL);
        text(60,80,"Switch application",GREEN,1);
        n=0;
        for(i=0;i<MZ_APPS;i++) if(s->open&(1u<<s->mru[i])) {
            if(i==s->chooser%MZ_APPS)rect(56,100+n*15,208,14,SELECT);
            text(64,103+n*15,mz_names[s->mru[i]],TEXT,1); n++;
        }
    }
    return 0;
}
int
mz_render(struct mz *s,struct mz_io *io)
{
    int i;
    for(i=0;i<MZ_BANDS;i++) if(s->xlo[i]<s->xhi[i]) {
        band_y=i*8;
        if(scene(s,io)<0 ||
            io->band(io->ctx,band_y,pixels,s->xlo[i],s->xhi[i])<0) return -1;
        s->xlo[i]=s->xhi[i]=0;
    }
    s->dirty=0;
    return 0;
}
