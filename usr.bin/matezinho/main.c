/* Original MATEzinho code is under the repository's BSD 3-Clause license. */
#include "matezinho.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef MZ_BOARD
#include <sys/ioctl.h>
#include <sys/lcdgfx.h>
#include <sys/lcdcons.h>
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <machine/cpu.h>
#include <machine/picocalcvar.h>
#include <sgtty.h>
#include <time.h>
#endif
#ifndef MZ_QEMU
#ifdef MZ_BOARD
#include <sys/dir.h>
#else
#include <dirent.h>
#endif
#endif

struct backend {
    int assets, display;
    long cached;
    unsigned char cache[1024];
};
static struct mz state;
static struct backend backend;
static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped=1; }
static int
transfer(int fd,void *data,int n,int writing)
{
    char *p=data;
    int k;
    while(n) {
        k=writing?(int)write(fd,p,(unsigned)n):(int)read(fd,p,(unsigned)n);
        if(k<0 && errno==EINTR) continue;
        if(k<=0) return -1;
        p+=k; n-=k;
    }
    return 0;
}
static int
asset_bytes(struct backend *b,long offset,unsigned char *out,int n)
{
    long base;
    int k;
    while(n) {
        base=offset&~1023L;
        if(b->cached!=base) {
            if(lseek(b->assets,base,SEEK_SET)!=base ||
                transfer(b->assets,b->cache,1024,0)<0) return -1;
            b->cached=base;
        }
        k=1024-(int)(offset-base);
        if(k>n) k=n;
        memcpy(out,b->cache+(offset-base),(unsigned)k);
        n-=k; offset+=k; out+=k;
    }
    return 0;
}
static int
read_icon(void *ctx,int app,int row,uint16_t *out)
{
    unsigned char raw[48];
    struct backend *b=ctx;
    int i;
    if(app<0||app>=9||row<0||row>=24) return -1;
    if(asset_bytes(b,1024L+app*2048L+row*48L,raw,48)<0) return -1;
    for(i=0;i<24;i++) out[i]=(uint16_t)(raw[i*2]|raw[i*2+1]<<8);
    return 0;
}
#ifdef MZ_BOARD
/* The console runs the panel at COLMOD 0x55; each open of /dev/lcdgfx resets
 * it to 0x66 (18 bits a pixel) and the last close restores 0x55. The desktop
 * sends RGB565 bands, so the open sets 0x55 again: two wire bytes a pixel. */
static int
open_display(void)
{
    struct lcdg_colmod cm;
    int fd=open("/dev/lcdgfx",O_WRONLY);
    if(fd<0) return -1;
    cm.fmt=LCDG_COLMOD_RGB565;
    if(ioctl(fd,LCDG_COLMOD,&cm)<0) { close(fd); return -1; }
    return fd;
}
#endif
/*
 * Send columns [xlo, xhi) (MZ_XUNIT-pixel units) of the 8 rows at y. The
 * board opens one window of that width and writes it row by row from the
 * 320-column band buffer. The PPM sink writes the same columns at the same
 * offsets, so the file holds the last range written to every pixel.
 */
static int
write_band(void *ctx,int y,const uint16_t *data,int xlo,int xhi)
{
    struct backend *b=ctx;
    int x0=xlo*MZ_XUNIT,w=(xhi-xlo)*MZ_XUNIT,j;
#ifdef MZ_BOARD
    struct lcdg_window win;
    memset(&win,0,sizeof(win));
    win.x=(unsigned short)x0; win.y=(unsigned short)y;
    win.w=(unsigned short)w; win.h=8; win.fmt=LCDG_WIN_RGB565;
    if(ioctl(b->display,LCDG_WINDOW,&win)<0) return -1;
    for(j=0;j<8;j++)
        if(transfer(b->display,(void *)(data+j*MZ_W+x0),w*2,1)<0) return -1;
    return 0;
#else
    unsigned char rgb[960];
    unsigned v;
    int i;
    for(j=0;j<8;j++) {
        for(i=0;i<w;i++) {
            v=data[j*MZ_W+x0+i];
            rgb[i*3]=(unsigned char)(((v>>11)*255+15)/31);
            rgb[i*3+1]=(unsigned char)((((v>>5)&63)*255+31)/63);
            rgb[i*3+2]=(unsigned char)(((v&31)*255+15)/31);
        }
        if(lseek(b->display,15L+(y+j)*960L+x0*3L,SEEK_SET)<0) return -1;
        if(transfer(b->display,rgb,w*3,1)<0) return -1;
    }
    return 0;
#endif
}
static void
files(struct mz *s)
{
#ifndef MZ_QEMU
    DIR *d;
#ifdef MZ_BOARD
    struct direct *e;
#else
    struct dirent *e;
#endif
    s->nfiles=0; s->file_cursor=0;
    d=opendir(s->location);
    if(!d) { strcpy(s->status,"Directory unavailable"); return; }
    while(s->nfiles<MZ_FILES && (e=readdir(d))!=NULL) {
        if(e->d_name[0]=='.' || strlen(e->d_name)>31) continue;
        snprintf(s->files[s->nfiles++],32,"%.31s",e->d_name);
    }
    closedir(d);
#else
    s->nfiles=1;
    strcpy(s->files[0],"Directory API: native backend");
#endif
}
#ifdef MZ_BOARD
/*
 * The machdep.picocalc light nodes. Any user reads them; the kernel takes a
 * write from root alone (__sysctl() in sys/kern/kern_sysctl.c), so another
 * user's change goes through backlight(1), which the backlight closure
 * installs setuid root. The vfork child points its output at /dev/null,
 * because the tty is in raw mode under the desktop, and drops the display
 * and artwork descriptors before the exec.
 */
static int
light_node(int which,int *level,int writing)
{
    int mib[3];
    size_t len=sizeof(*level);
    mib[0]=CTL_MACHDEP; mib[1]=CPU_PICOCALC;
    mib[2]=which==MZ_KEYS?CPU_PICOCALC_KBD_BACKLIGHT:CPU_PICOCALC_LCD_BACKLIGHT;
    if(writing) return sysctl(mib,3,NULL,NULL,level,sizeof(*level));
    if(sysctl(mib,3,level,&len,NULL,0)<0) return -1;
    return len==sizeof(*level)?0:-1;
}
static int
light_get(int which,int *level)
{
    return light_node(which,level,0);
}
static const char *
light_set(int which,int level)
{
    char arg[4];
    int pid,reaped,wait_status,fd;
    if(light_node(which,&level,1)==0) return NULL;
    if(errno!=EPERM) return "Light refused by the kernel";
    snprintf(arg,sizeof(arg),"%d",level);
    pid=vfork();
    if(pid==0) {
        fd=open("/dev/null",O_WRONLY);
        if(fd>=0) { dup2(fd,1); dup2(fd,2); if(fd>2) close(fd); }
        if(backend.display>=0) close(backend.display);
        close(backend.assets);
        execl("/usr/bin/backlight","backlight",which==MZ_KEYS?"kbd":"lcd",arg,(char *)0);
        _exit(127);
    }
    if(pid<0) return "No process for backlight(1)";
    do reaped=wait(&wait_status);
    while(reaped!=pid && (reaped>=0 || errno==EINTR));
    if(reaped!=pid || !WIFEXITED(wait_status)) return "backlight(1) did not finish";
    if(WEXITSTATUS(wait_status)==127) return "backlight(1) is not installed";
    return WEXITSTATUS(wait_status)?"backlight(1) refused the level":NULL;
}
#else
/*
 * The development and QEMU builds keep the two levels in this stand-in,
 * which conditions a write as the controller's firmware does (backlight.ino): the
 * screen at a multiple of 16 within 16-240, the keys at a multiple of 32,
 * 0 below 32. The screen starts at 160, the level /etc/rc sets at boot.
 */
static int model_light[2]={160,0};
static int
light_get(int which,int *level)
{
    *level=model_light[which];
    return 0;
}
static const char *
light_set(int which,int level)
{
    if(which==MZ_KEYS) {
        level=level/MZ_KEYS_STEP*MZ_KEYS_STEP;
        if(level<MZ_KEYS_STEP) level=0;
    } else {
        level=level/MZ_SCREEN_STEP*MZ_SCREEN_STEP;
        if(level<MZ_SCREEN_STEP) level=MZ_SCREEN_STEP;
    }
    model_light[which]=level;
    return NULL;
}
#endif
/* Read both lights; mark Settings when a level differs from the shown one. */
static void
lights(struct mz *s)
{
    int i,v;
    for(i=0;i<2;i++) {
        if(light_get(i,&v)<0) v=-1;
        if(v!=s->light[i]) { s->light[i]=v; mz_all(s); }
    }
}
/* Write the levels Left and Right asked for, then show what was kept. */
static void
flush_lights(struct mz *s)
{
    const char *failure=NULL,*why;
    int i,last=-1;
    if(s->light_want[MZ_SCREEN]<0 && s->light_want[MZ_KEYS]<0) return;
    for(i=0;i<2;i++) if(s->light_want[i]>=0) {
        why=light_set(i,s->light_want[i]);
        if(why) failure=why;
        else last=i;
        s->light_want[i]=-1;
    }
    lights(s);
    if(failure) snprintf(s->status,sizeof(s->status),"%s",failure);
    else if(last>=0 && s->light[last]>=0)
        snprintf(s->status,sizeof(s->status),"%s light %d",
            last==MZ_KEYS?"Keyboard":"Screen",s->light[last]);
    mz_all(s);
}
static void
calculate(struct mz *s)
{
    long a,b,v=0;
    char op,*p,*q;
    int valid=1;
    errno=0; a=strtol(s->expr,&p,10);
    if(p==s->expr||errno) valid=0;
    while(*p==' ')p++;
    op=*p;
    if(!op) valid=0;
    else p++;
    errno=0; b=strtol(p,&q,10);
    if(p==q||errno) valid=0;
    while(*q==' ')q++;
    if(*q)valid=0;
    /* Bounded operands make every intermediate safe at target ILP32. */
    if(a < -32767 || a>32767 || b < -32767 || b>32767) valid=0;
    if(valid) switch(op) {
        case '+':v=a+b;break; case '-':v=a-b;break; case '*':v=a*b;break;
        case '/':if(b)v=a/b;else valid=0;break;
        default:valid=0;
    }
    if(valid) snprintf(s->result,sizeof(s->result),"= %ld",v);
    else strcpy(s->result,"Invalid / operands exceed +/-32767");
    mz_all(s);
}
static void
save_note(const char *path)
{
    char temp[160];
    int fd,ok=0;
    if(strlen(path)>140) {strcpy(state.status,"Note path too long");mz_all(&state);return;}
    snprintf(temp,sizeof(temp),"%s.new",path);
    fd=open(temp,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd>=0) {
        ok=transfer(fd,state.note,(int)strlen(state.note),1)==0;
        if(close(fd)<0) ok=0;
        if(ok) ok=rename(temp,path)==0;
        if(!ok)unlink(temp);
    }
    strcpy(state.status,ok?"Note saved":"Save failed; note retained");
    if(ok) state.note_dirty=0;
    mz_all(&state);
}
static void
apply(int key,const char *note_path)
{
    int was=state.active;
    if(key==MZ_SAVE) { if(state.active==1)save_note(note_path); return; }
    if(key==MZ_REFRESH) { files(&state); lights(&state); mz_all(&state); return; }
    /* The keyboard's own shortcuts change the levels behind the desktop. */
    if(key==MZ_TICK && state.active==4 && !state.menu) lights(&state);
    if(state.active==3 && (key=='\n'||key=='\r')) { calculate(&state); return; }
    if(state.active==0 && !state.menu) {
        if(key==MZ_UP && state.file_cursor>0)state.file_cursor--;
        else if(key==MZ_DOWN && state.file_cursor+1<state.nfiles)state.file_cursor++;
#ifndef MZ_QEMU
        else if((key=='\n'||key=='\r') && state.nfiles) {
            char next[128];
            struct stat st;
            if(strlen(state.location)+strlen(state.files[state.file_cursor])+2<sizeof(next)) {
                strcpy(next,state.location);
                if(strcmp(state.location,"/"))strcat(next,"/");
                strcat(next,state.files[state.file_cursor]);
                if(stat(next,&st)==0 && S_ISDIR(st.st_mode)) {
                    strcpy(state.location,next); files(&state);
                } else strcpy(state.status,"Select a directory to enter");
            }
        } else if(key==8 || key==127) {
            char *last=strrchr(state.location,'/');
            if(last && last!=state.location)*last=0;
            else strcpy(state.location,"/");
            files(&state);
        }
#endif
        if(key==MZ_UP||key==MZ_DOWN||key=='\n'||key=='\r'||key==8||key==127) {
            mz_all(&state);return;
        }
    }
    mz_action(&state,key);
    if(state.active==4 && was!=4) lights(&state);
}
#ifdef MZ_BOARD
static int
board_loop(struct mz_io *io,const char *note_path)
{
    struct sgttyb saved,raw;
    struct lcd_keyevs ev;
    int on=1,off=0,pgrp,i,key,ctrl=0,own=1,status=0;
    time_t before,now;
    if(ioctl(0,TIOCGETP,&saved)<0 || ioctl(0,TIOCGPGRP,&pgrp)<0 ||
        pgrp!=getpgrp()) return -1;
    raw=saved; raw.sg_flags|=RAW; raw.sg_flags&=~ECHO;
    if(ioctl(0,TIOCSETP,&raw)<0) return -1;
    if(ioctl(0,LCDIOC_KEYEVS,&on)<0) { ioctl(0,TIOCSETP,&saved); return -1; }
    before=time(NULL);
    while(!stopped) {
        if(ioctl(0,LCDIOC_GETKEY,&ev)<0) { own=0; status=-1; break; }
        if(ev.lost) { ctrl=0; state.chooser=0; mz_all(&state); }
        for(i=0;i<ev.n;i++) {
            key=ev.ev[i].key;
            if(key==LCD_KEY_CTRL) {
                ctrl=ev.ev[i].state!=LCD_KEY_RELEASED;
                if(!ctrl)apply(MZ_COMMIT,note_path);
                continue;
            }
            if(ev.ev[i].state==LCD_KEY_RELEASED)continue;
            if(ctrl && (key=='q'||key=='Q')) { stopped=1; break; }
            if(ctrl && key==LCD_KEY_TAB)key=MZ_SWITCH;
            else if(ctrl && (key=='w'||key=='W'))key=MZ_CLOSE;
            else if(ctrl && (key=='s'||key=='S'))key=MZ_SAVE;
            else if(key==LCD_KEY_LEFT)key=MZ_LEFT;
            else if(key==LCD_KEY_RIGHT)key=MZ_RIGHT;
            else if(key==LCD_KEY_UP)key=MZ_UP;
            else if(key==LCD_KEY_DOWN)key=MZ_DOWN;
            else if(key==LCD_KEY_ESC)key=MZ_HOME;
            else if(key==0x81)key=MZ_MENU;
            else if(key==0x85)key=MZ_REFRESH;
            if(state.active==6 && key==LCD_KEY_ENTER) {
                int pid,reaped,wait_status;
                ioctl(0,LCDIOC_KEYEVS,&off);
                ioctl(0,TIOCSETP,&saved);
                close(backend.display); backend.display=-1;
                pid=fork();
                if(pid==0) {
                    close(backend.assets);
                    execl("/bin/sh","sh",(char *)0); _exit(127);
                }
                if(pid>0) do reaped=wait(&wait_status);
                    while(reaped!=pid && (reaped>=0 || errno==EINTR));
                backend.display=open_display();
                /* A tty whose foreground group changed during the shell
                 * keeps the modes its new owner set. */
                if(backend.display<0 || ioctl(0,TIOCGPGRP,&pgrp)<0 ||
                    pgrp!=getpgrp() || ioctl(0,TIOCSETP,&raw)<0) {
                    own=0; status=-1; stopped=1; break;
                }
                /* The tty is RAW here; a refused key session leaves no owner
                 * to undo that at the end of the loop. */
                if(ioctl(0,LCDIOC_KEYEVS,&on)<0) {
                    ioctl(0,TIOCSETP,&saved);
                    own=0; status=-1; stopped=1; break;
                }
                mz_all(&state);
            } else apply(key,note_path);
        }
        /* One write per light for the whole batch of keys. */
        if(state.light_want[MZ_SCREEN]>=0 || state.light_want[MZ_KEYS]>=0)
            flush_lights(&state);
        now=time(NULL);
        if(now>before) { state.ticks+=(unsigned)(now-before-1); apply(MZ_TICK,note_path); }
        before=now;
        if(state.dirty && mz_render(&state,io)<0) {status=-1;break;}
        usleep(20000);
    }
    /* A revoked session's new owner owns its tty modes and queues. */
    if(own && ioctl(0,LCDIOC_GETKEY,&ev)==0) {
        ioctl(0,LCDIOC_KEYEVS,&off);
        ioctl(0,TIOCSETP,&saved);
        ioctl(0,TIOCFLUSH,&off);
    }
    return status;
}
#endif
int
main(int argc,char **argv)
{
    struct mz_io io;
    unsigned char header[16];
    int status=0;
#ifndef MZ_BOARD
    int i;
#endif
    const char *note_path;
    if(argc<3) {
        fprintf(stderr,"usage: matezinho assets.mzar output.ppm [key ...]\n"
            "PicoCalc: matezinho assets.mzar note-path\n");
        return 2;
    }
    backend.cached=-1;
    backend.assets=open(argv[1],O_RDONLY);
    if(backend.assets<0 || asset_bytes(&backend,0,header,16)<0 ||
        memcmp(header,"MZAR0001\x09\x18\x18\x02",12)!=0) {
        fprintf(stderr,"matezinho: invalid or unavailable artwork volume\n");
        if(backend.assets>=0)close(backend.assets);
        return 1;
    }
#ifdef MZ_BOARD
    backend.display=open_display();
    note_path=argv[2];
#else
    backend.display=open(argv[2],O_CREAT|O_TRUNC|O_RDWR,0600);
    note_path="matezinho-note.txt";
#endif
    if(backend.display<0) {close(backend.assets);return 1;}
#ifndef MZ_BOARD
    if(transfer(backend.display,(void *)"P6\n320 320\n255\n",15,1)<0)status=1;
#endif
    mz_init(&state);
    state.asset_sectors=(unsigned)header[12]|(unsigned)header[13]<<8|
        (unsigned)header[14]<<16|(unsigned)header[15]<<24;
    {
        int note=open(note_path,O_RDONLY),n;
        if(note>=0) {
            n=(int)read(note,state.note,MZ_NOTES-1);
            if(n>0)state.note[n]=0;
            close(note);
        }
    }
    files(&state);
    io.ctx=&backend;io.icon=read_icon;io.band=write_band;
    signal(SIGINT,stop);signal(SIGTERM,stop);
    if(!status && mz_render(&state,&io)<0)status=1;
#ifdef MZ_BOARD
    signal(SIGTTOU,SIG_IGN);
    signal(SIGHUP,stop);signal(SIGQUIT,stop);
    if(!status && board_loop(&io,note_path)<0)status=1;
#else
    for(i=3;i<argc&&!status;i++) {
        apply(atoi(argv[i]),note_path);
        flush_lights(&state);
        if(mz_render(&state,&io)<0)status=1;
    }
    if(argc==3&&!status) {
        char line[16],ch;
        int n=0;
        if(write(1,"frame\n",6)!=6)status=1;
        while(!status&&!stopped && read(0,&ch,1)==1) {
            if(ch=='\n') {
                line[n]=0;n=0;
                if(!strcmp(line,"quit"))break;
                apply(atoi(line),note_path);
                flush_lights(&state);
                if(mz_render(&state,&io)<0 || write(1,"frame\n",6)!=6)status=1;
            } else if(n<15)line[n++]=ch;
            else {status=1;break;}
        }
    }
#endif
    close(backend.display);close(backend.assets);
    return status;
}
