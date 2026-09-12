/*
 * menu -- a full-screen menu launcher for the USB serial console.
 *
 * Design note: this reproduces the shape of two ideas from Rockbox's
 * user interface (apps/action.c's keymap-to-action dispatch and
 * apps/gui/list.c's cursor-tracking list widget), studied from Rockbox's
 * own source tree for their structure only, in
 * sys/arch/rp2040/doc/research/rockbox-ui.md. No Rockbox source, header,
 * or comment text is copied; every line below is original and BSD-licensed
 * like the rest of this tree. The mapping:
 *
 *   - struct keymap[] and key_to_action() stand in for action.c's keymap
 *     table: a key code is looked up once and turned into an abstract
 *     ACT_* verb, so the rest of the program never tests a raw key value.
 *   - struct list and list_draw()/list_move() stand in for list.c's list
 *     widget: they track a cursor index, a scroll offset (top) and an
 *     item count, and redraw only the rows that changed (the old and new
 *     cursor row on a plain move, the whole viewport on a scroll), taking
 *     a "get label by index" callback the way list.c takes a callback
 *     into the item it is showing instead of owning the item storage.
 *   - Everything menu-specific -- entry storage, /etc/menu parsing, the
 *     built-in actions, running a command -- is new code with no Rockbox
 *     analog; Rockbox's menu.c also builds submenus and settings screens
 *     this program does not need.
 *
 * Terminal handling follows usr.bin/kilo/kilo.c and usr.bin/re/r.ttyio.c:
 * sgtty(4) TIOCGETP/TIOCSETP/TIOCGETC/TIOCSETC/TIOCGLTC/TIOCSLTC raw mode
 * on the target, real <termios.h> on the host build (TERMIOS macro), and
 * the same FIONREAD poll after a lone ESC that CBREAK's lack of VMIN/VTIME
 * requires. The screen is driven with the same three ANSI sequences kilo
 * uses (\x1b[2J clear, \x1b[r;cH cursor position, \x1b[7m reverse video),
 * assuming an 80x24 terminal emulator over CDC-ACM as documented in
 * sys/arch/rp2040/doc/STORAGE.md and the port's console setup -- no
 * libcurses, no terminal-size query.
 */

#ifdef TERMIOS
#include <termios.h>
#else
#include <sgtty.h>
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/dir.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Static allocation only: a fixed entry cap, bounded label and command
 * strings, no malloc growth. See sys/arch/rp2040/doc/STORAGE.md on the
 * a.out and bss budget this program stays inside.
 */
#define MAX_ENTRIES     192
#define LABEL_MAX       32
#define CMD_MAX         80
#define SCREEN_ROWS     24
#define SCREEN_COLS     80
#define LIST_TOP_ROW    3       /* first row the list occupies (1-based) */
#define LIST_ROWS       (SCREEN_ROWS - LIST_TOP_ROW - 1)  /* leaves a status row */
#define STATUS_ROW      SCREEN_ROWS
#define DEFAULT_MENU_FILE "/etc/menu"

struct entry {
    char label[LABEL_MAX];
    char cmd[CMD_MAX];      /* empty for a built-in that prompts for input */
    int is_builtin;         /* index into builtin_prompt[]/builtin_fmt[], or -1 */
};

static struct entry entries[MAX_ENTRIES];
static int nentries;

/* ======================= Terminal raw mode (kilo/re pattern) ======================= */

enum {
    KEY_NULL = 0,
    CTRL_C = 3,
    ENTER = 13,
    ESC = 27,
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

static int rawmode;

#ifdef TERMIOS
static struct termios orig_termios;
#else
static struct sgttyb orig_sgttyb;
static struct tchars orig_tchars;
static struct ltchars orig_ltchars;
#endif

static void disable_raw_mode(int fd)
{
    if (!rawmode)
        return;
#ifdef TERMIOS
    tcsetattr(fd, TCSAFLUSH, &orig_termios);
#else
    ioctl(fd, TIOCSETP, &orig_sgttyb);
    ioctl(fd, TIOCSETC, &orig_tchars);
    ioctl(fd, TIOCSLTC, &orig_ltchars);
#endif
    rawmode = 0;
}

static void at_exit_restore(void)
{
    disable_raw_mode(STDIN_FILENO);
    /* Leave the screen usable: unreversed, cursor below the drawn area. */
    printf("\x1b[0m\x1b[%d;1H\n", SCREEN_ROWS);
}

static int enable_raw_mode(int fd)
{
    if (rawmode)
        return 0;
    if (!isatty(fd)) {
        errno = ENOTTY;
        return -1;
    }
    atexit(at_exit_restore);

#ifdef TERMIOS
    struct termios raw;

    if (tcgetattr(fd, &orig_termios) == -1)
        return -1;
    raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
        return -1;
#else
    struct sgttyb sg;
    struct tchars tc;
    struct ltchars ltc;

    if (ioctl(fd, TIOCGETP, &orig_sgttyb) == -1)
        return -1;
    if (ioctl(fd, TIOCGETC, &orig_tchars) == -1)
        return -1;
    if (ioctl(fd, TIOCGLTC, &orig_ltchars) == -1)
        return -1;

    sg = orig_sgttyb;
    sg.sg_flags &= ~(ECHO | CRMOD | XTABS | RAW);
    sg.sg_flags |= CBREAK;
    if (ioctl(fd, TIOCSETP, &sg) == -1)
        return -1;

    tc = orig_tchars;
    tc.t_intrc = -1;
    tc.t_quitc = -1;
    tc.t_startc = -1;
    tc.t_stopc = -1;
    tc.t_eofc = -1;
    ioctl(fd, TIOCSETC, &tc);

    ltc = orig_ltchars;
    ltc.t_suspc = -1;
    ltc.t_dsuspc = -1;
    ltc.t_rprntc = -1;
    ltc.t_flushc = -1;
    ltc.t_werasc = -1;
    ltc.t_lnextc = -1;
    ioctl(fd, TIOCSLTC, &ltc);
#endif
    rawmode = 1;
    return 0;
}

/* Read one key, decoding ESC [ A/B/C/D arrow sequences the way
 * kilo.c's editorReadKey does: sgtty CBREAK has no VMIN/VTIME timeout,
 * so a second byte's presence is polled with FIONREAD instead of being
 * awaited blindly, and a lone ESC with nothing queued behind it returns
 * immediately as ESC.
 */
static int read_key(int fd)
{
    int nread;
    char c, seq[2];
    long avail;

    while ((nread = read(fd, &c, 1)) == 0)
        ;
    if (nread == -1)
        exit(1);

    if (c != ESC)
        return (unsigned char)c;

    if (ioctl(fd, FIONREAD, &avail) == -1 || avail == 0)
        return ESC;
    if (read(fd, seq, 1) == 0)
        return ESC;
    if (ioctl(fd, FIONREAD, &avail) == -1 || avail == 0)
        return ESC;
    if (read(fd, seq + 1, 1) == 0)
        return ESC;

    if (seq[0] == '[') {
        switch (seq[1]) {
        case 'A': return ARROW_UP;
        case 'B': return ARROW_DOWN;
        case 'C': return ARROW_RIGHT;
        case 'D': return ARROW_LEFT;
        }
    }
    return ESC;
}

/* ======================= Action mapping (Rockbox action.c's idea) ======================= */

enum action {
    ACT_NONE = 0,
    ACT_UP,
    ACT_DOWN,
    ACT_SELECT,
    ACT_BACK,
    ACT_QUIT
};

struct keymap {
    int key;
    enum action action;
};

/* One table, one place to change a binding. Everything past read_key()
 * deals in enum action, never in a raw key code -- the same separation
 * action.c draws between the button layer and the rest of the UI.
 */
static const struct keymap keymap[] = {
    { 'k',          ACT_UP },
    { ARROW_UP,     ACT_UP },
    { 'j',          ACT_DOWN },
    { ARROW_DOWN,   ACT_DOWN },
    { ENTER,        ACT_SELECT },
    { '\n',         ACT_SELECT },
    { ' ',          ACT_SELECT },
    { 'q',          ACT_QUIT },
    { CTRL_C,       ACT_QUIT },
    { ESC,          ACT_BACK },
};
#define NKEYMAP (int)(sizeof(keymap) / sizeof(keymap[0]))

static enum action key_to_action(int key)
{
    int i;

    for (i = 0; i < NKEYMAP; i++)
        if (keymap[i].key == key)
            return keymap[i].action;
    return ACT_NONE;
}

/* ======================= List widget (Rockbox list.c's idea) ======================= */

/* Tracks selection, scroll offset and item count only; it owns no item
 * storage and knows no menu semantics, matching list.c's split between
 * the widget and whatever screen puts it up. get_label(i) renders item i
 * into a caller-supplied buffer, list.c's role for its own callback.
 */
struct list {
    int count;
    int cursor;
    int top;
    int visible;
    void (*get_label)(int index, char *buf, size_t buflen);
};

static void list_init(struct list *l, int count, int visible,
                       void (*get_label)(int, char *, size_t))
{
    l->count = count;
    l->cursor = 0;
    l->top = 0;
    l->visible = visible;
    l->get_label = get_label;
}

/* Redraws one row at screen row (LIST_TOP_ROW + slot), item index, in
 * reverse video when it carries the cursor.
 */
static void list_draw_row(struct list *l, int slot, int reversed)
{
    char buf[LABEL_MAX + 8];
    int index = l->top + slot;

    printf("\x1b[%d;1H", LIST_TOP_ROW + slot);
    if (index >= l->count) {
        printf("\x1b[K");
        return;
    }
    l->get_label(index, buf, sizeof(buf));
    if (reversed)
        printf("\x1b[7m %-78.78s\x1b[0m", buf);
    else
        printf(" %-78.78s\x1b[K", buf);
}

/* Full-viewport redraw: used on scroll (top changed) and on first paint. */
static void list_draw(struct list *l)
{
    int slot;

    for (slot = 0; slot < l->visible; slot++)
        list_draw_row(l, slot, l->top + slot == l->cursor);
}

/* Moves the cursor by delta (+1/-1), scrolling top if the cursor would
 * leave the visible window. Redraws only the rows that actually change:
 * the old and new cursor rows on a plain move within the window, the
 * whole window on a scroll -- the incremental-redraw idea list.c's
 * scroll/cursor drawing follows, reproduced without its code.
 */
static void list_move(struct list *l, int delta)
{
    int old_cursor = l->cursor;
    int new_cursor = old_cursor + delta;

    if (l->count == 0)
        return;
    if (new_cursor < 0)
        new_cursor = 0;
    if (new_cursor >= l->count)
        new_cursor = l->count - 1;
    if (new_cursor == old_cursor)
        return;
    l->cursor = new_cursor;

    if (new_cursor < l->top || new_cursor >= l->top + l->visible) {
        if (new_cursor < l->top)
            l->top = new_cursor;
        else
            l->top = new_cursor - l->visible + 1;
        list_draw(l);
        return;
    }

    list_draw_row(l, old_cursor - l->top, 0);
    list_draw_row(l, new_cursor - l->top, 1);
}

/* ======================= Entries: built-ins + /etc/menu ======================= */

enum builtin_kind {
    BI_NONE = -1,
    BI_RUN = 0,
    BI_SHOW,
    BI_LIST
};

struct builtin_desc {
    const char *label;
    const char *prompt;
    const char *cmd_fmt;   /* %s takes the prompted argument */
    const char *default_arg;
};

static const struct builtin_desc builtins[] = {
    { "Run a program",       "Program: ",   "%s",       "" },
    { "Show a file (pager)", "File: ",      "more %s",  "/etc/motd" },
    { "List a directory",    "Directory: ", "ls -l %s", "." },
};
#define NBUILTINS (int)(sizeof(builtins) / sizeof(builtins[0]))

/* Parses "label: command" lines from path into entries[], starting after
 * the built-ins already installed by add_builtins(). A line starting
 * with '#', or with no ':', is skipped -- a comment or malformed line
 * costs nothing but itself. Leading/trailing blanks around the label and
 * the command are trimmed.
 */
static void load_config(const char *path)
{
    FILE *fp;
    char line[LABEL_MAX + CMD_MAX + 4];

    fp = fopen(path, "r");
    if (fp == NULL)
        return;

    while (nentries < MAX_ENTRIES && fgets(line, sizeof(line), fp) != NULL) {
        char *colon, *label, *cmd, *end;

        end = line + strlen(line);
        while (end > line && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = '\0';

        label = line;
        while (*label == ' ' || *label == '\t')
            label++;
        if (*label == '\0' || *label == '#')
            continue;

        colon = strchr(label, ':');
        if (colon == NULL)
            continue;
        *colon = '\0';
        cmd = colon + 1;
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;

        end = label + strlen(label);
        while (end > label && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';

        if (*label == '\0' || *cmd == '\0')
            continue;

        entries[nentries].is_builtin = BI_NONE;
        strlcpy(entries[nentries].label, label, sizeof(entries[nentries].label));
        strlcpy(entries[nentries].cmd, cmd, sizeof(entries[nentries].cmd));
        nentries++;
    }
    fclose(fp);
}

/*
 * Fill the menu with every command on the PATH so the whole system is
 * one scrollable list, the box multicall binaries decomposed into the
 * names they answer to. A name already present -- a built-in, a config
 * entry, or a command reached through an earlier directory -- is not
 * added twice, and the box multiplexers themselves are hidden because
 * running one by its own name only prints "no such tool".
 */
static const char *const prog_dirs[] = {
    "/bin", "/usr/bin", "/sbin", "/usr/sbin", "/usr/games"
};
#define NPROGDIRS (int)(sizeof(prog_dirs) / sizeof(prog_dirs[0]))

static int is_box(const char *name)
{
    size_t n = strlen(name);
    return n >= 3 && strcmp(name + n - 3, "box") == 0;
}

static int already_listed(const char *name)
{
    int i;
    for (i = 0; i < nentries; i++)
        if (strcmp(entries[i].cmd, name) == 0 ||
            strcmp(entries[i].label, name) == 0)
            return 1;
    return 0;
}

static int prog_cmp(const void *a, const void *b)
{
    return strcmp(((const struct entry *)a)->label,
                  ((const struct entry *)b)->label);
}

static void scan_programs(void)
{
    int d, start = nentries;
    DIR *dir;
    struct direct *dp;

    for (d = 0; d < NPROGDIRS; d++) {
        dir = opendir(prog_dirs[d]);
        if (dir == NULL)
            continue;
        while ((dp = readdir(dir)) != NULL && nentries < MAX_ENTRIES) {
            if (dp->d_ino == 0 || dp->d_name[0] == '.')
                continue;
            if (is_box(dp->d_name) || already_listed(dp->d_name))
                continue;
            entries[nentries].is_builtin = BI_NONE;
            strlcpy(entries[nentries].label, dp->d_name, LABEL_MAX);
            strlcpy(entries[nentries].cmd, dp->d_name, CMD_MAX);
            nentries++;
        }
        closedir(dir);
    }
    /* Sort just the scanned range so the programs read alphabetically
     * while the built-ins and config entries keep their order on top. */
    if (nentries > start + 1)
        qsort(&entries[start], nentries - start, sizeof(entries[0]),
              prog_cmp);
}

static void add_builtins(void)
{
    int i;

    for (i = 0; i < NBUILTINS && nentries < MAX_ENTRIES; i++) {
        entries[nentries].is_builtin = i;
        strlcpy(entries[nentries].label, builtins[i].label,
                sizeof(entries[nentries].label));
        entries[nentries].cmd[0] = '\0';
        nentries++;
    }
}

static void entry_label(int index, char *buf, size_t buflen)
{
    snprintf(buf, buflen, "%s", entries[index].label);
}

/* ======================= Screen chrome ======================= */

static void draw_chrome(void)
{
    printf("\x1b[2J\x1b[1;1H");
    printf("\x1b[7m DiscoBSD menu -- j/k or arrows move, Enter runs, q quits \x1b[0m\x1b[K");
    printf("\x1b[2;1H\x1b[K");
    printf("\x1b[%d;1H\x1b[K", STATUS_ROW);
}

static void set_status(const char *msg)
{
    printf("\x1b[%d;1H\x1b[K%.*s", STATUS_ROW, SCREEN_COLS - 1, msg);
    fflush(stdout);
}

/* Reads one line of input on the status row, echoing keystrokes itself
 * (ECHO is off in raw mode) and handling backspace. Returns 0 on Enter,
 * -1 if the input was cancelled with ESC.
 */
static int prompt_line(int fd, const char *prompt, char *buf, size_t buflen,
                        const char *initial)
{
    size_t len;

    strlcpy(buf, initial ? initial : "", buflen);
    len = strlen(buf);

    for (;;) {
        printf("\x1b[%d;1H\x1b[K%s%s", STATUS_ROW, prompt, buf);
        fflush(stdout);

        int key = read_key(fd);

        if (key == ENTER || key == '\n') {
            return 0;
        } else if (key == ESC) {
            return -1;
        } else if (key == BACKSPACE || key == 8) {
            if (len > 0)
                buf[--len] = '\0';
        } else if (key >= 32 && key < 127 && len + 1 < buflen) {
            buf[len++] = (char)key;
            buf[len] = '\0';
        }
        /* Any other key (arrows, etc.) is ignored on the prompt line. */
    }
}

/* Runs command through /bin/sh -c so pipes, redirection and box tools
 * all work, the way an interactive shell would run it: fork(), execlp()
 * in the child, wait() in the parent, the same three-call shape
 * bin/ed/ed.c's callunix() uses for "!" shell escapes on this tree.
 */
static void run_command(const char *command)
{
    pid_t pid;
    int status;

    disable_raw_mode(STDIN_FILENO);
    printf("\x1b[2J\x1b[1;1H");
    fflush(stdout);

    pid = fork();
    if (pid < 0) {
        printf("menu: fork failed\r\n");
        fflush(stdout);
    } else if (pid == 0) {
        execlp("/bin/sh", "sh", "-c", command, (char *)NULL);
        printf("menu: exec /bin/sh failed\r\n");
        fflush(stdout);
        _exit(127);
    } else {
        while (wait(&status) != pid)
            ;
    }

    printf("\r\n[command finished -- press any key]");
    fflush(stdout);
    enable_raw_mode(STDIN_FILENO);
    read_key(STDIN_FILENO);
}

/* Runs the selected entry: a static /etc/menu command runs as-is; a
 * built-in prompts for its one argument first and formats it into the
 * built-in's command template.
 */
static void run_entry(int fd, struct entry *e)
{
    char arg[CMD_MAX];
    char command[CMD_MAX];
    const struct builtin_desc *b;

    if (e->is_builtin == BI_NONE) {
        run_command(e->cmd);
        return;
    }

    b = &builtins[e->is_builtin];
    if (prompt_line(fd, b->prompt, arg, sizeof(arg), b->default_arg) != 0) {
        set_status("cancelled");
        return;
    }
    snprintf(command, sizeof(command), b->cmd_fmt, arg);
    run_command(command);
}

int main(int argc, char **argv)
{
    const char *menu_file = DEFAULT_MENU_FILE;
    struct list list;

    if (argc > 1)
        menu_file = argv[1];

    add_builtins();
    load_config(menu_file);
    scan_programs();

    if (enable_raw_mode(STDIN_FILENO) == -1) {
        fprintf(stderr, "menu: not a terminal\n");
        return 1;
    }

    list_init(&list, nentries, LIST_ROWS, entry_label);
    draw_chrome();
    list_draw(&list);
    fflush(stdout);

    for (;;) {
        int key = read_key(STDIN_FILENO);
        enum action act = key_to_action(key);

        switch (act) {
        case ACT_UP:
            list_move(&list, -1);
            fflush(stdout);
            break;
        case ACT_DOWN:
            list_move(&list, 1);
            fflush(stdout);
            break;
        case ACT_SELECT:
            if (list.count > 0)
                run_entry(STDIN_FILENO, &entries[list.cursor]);
            draw_chrome();
            list_draw(&list);
            fflush(stdout);
            break;
        case ACT_BACK:
            /* No submenus in this build: back at the top level quits,
             * the same way it would pop past the root menu if one
             * existed above it.
             */
        case ACT_QUIT:
            disable_raw_mode(STDIN_FILENO);
            return 0;
        case ACT_NONE:
        default:
            break;
        }
    }
}
