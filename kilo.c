/*** includes ***/

/* feature test macro */
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/*** data ***/

typedef struct {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

typedef struct {
    /* cx: horizontal index of cursor in file */
    /* cy: vertical index of cursor in file */
    int cx, cy;
    int rx; // current render index of the cursor
    int rowOff;
    int colOff;
    int screenRows;
    int screenCols;
    int numRows;
    erow *row;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    /* termios struct from termios.h to manipulate terminal's atributes */
    struct termios orig_termios;
} editorConfig;

editorConfig E;

/*** terminal ***/

/* error handling */
void die(const char *s) {
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    /* print s then exit with code 1 */
    perror(s);
    exit(1);
}

/* disable raw mode (return to canonical mode)*/
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/* canonical mode: input are sent after '\n'*/
/* raw mode: read byte-by-byte instead of line-by-line, enabled by turn off many flags in the terminal*/
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) 
        die("tcgetattr");
     /*execute disableRawMode function when receive exit signal*/
    atexit(disableRawMode);
    
    struct termios raw = E.orig_termios;

     /* C_iflag: input flag */
     /* BRKINT when on, break condition cause SIGINT (like Ctrl-C) */
     /* ICRNL disable Ctrl-M */
     /* INPCK enable parity checking (don't apply to modern terminala emulators) */
     /* ISTRIP strip 8th bit of each input byte */
     /* IXON disable Ctrl-S and Ctrl-Q */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    /* C_oflag: output flag */
    /* turn off output processing ('\n' -> '\r\n') */
    raw.c_oflag &= ~(OPOST);

    /* character size (CS) to 8 bits per byte */
    raw.c_cflag |= (CS8);

    /* c_lflag: local flag */
    /* disable ECHO bitflag (see what you type in the terminal) */
            /* ICANON canonical mode */
            /* IEXTEN Ctrl-V */
            /* ISIG Ctrl-C and Ctrl-Z */
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    /* set timeout for read() ;*/
    raw.c_cc[VMIN] = 0; // min bytes needed before read() can return;
    raw.c_cc[VTIME] = 1; // max time to wait before read() return;

    /* apply to your terminal with tcsetattr and TCSAFLUSH */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
        die("tcsetattr");
}

int editorReadKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    

    /*Escape code process*/

    if (c == '\x1b') {
        /* vt sequences: */
        /* Home: /x1b[1~ or /x1b[7~ */
        /* Del: /x1b[3~ */
        /* End: /x1b[4~ or /x1b[8~ */
        /* PgUp: /x1b[6~ */
        /* PgDn: /x1b[7~ */
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else {
                /* xterm sequences: */
                /* Up: /x1b[A */
                /* Down: /x1b[B */
                /* Right: /x1b[C */
                /* Left: /x1b[D */
                /* Home: /x1b[H */
                /* End: /x1b[F */
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        }
        else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY; // /x1bOH
                case 'F': return END_KEY; // /x1bOF
            }
        }

        return '\x1b';
    }
    else {
        return c;
    }

}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; //  VT100 cursor position report 

    printf("\r\n");
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        ++i;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        /*ioctl doesn't guarantee to work on all system*/
        /*so we move the cursor to the last cell and report the cursor position*/
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols); 
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** row operations ***/

int editorRowCxToRx(erow* row, int cx) {
    int rx = 0;
    for (int j = 0; j < cx; ++j) {
        if (row->chars[j] == '\t')
            rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
        ++rx;
    }
    return rx;
}

void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (int j = 0; j < row->size; ++j) {
        if (row->chars[j] == '\t') ++tabs;
    }

    free(row->render);
    row->render = malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);

    int idx = 0;
    for (j = 0; j < row->size; ++j) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % 8 != 0) row->render[idx++] = ' ';
        }
        else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numRows + 1));

    int at = E.numRows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';

    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);

    ++E.numRows;
}

/*** file i/o ***/

void editorOpen(char * filename) {
    free(E.filename);
    E.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char *line = NULL;
    size_t lineCap = 0;
    ssize_t lineLen;
    while ((lineLen = getline(&line, &lineCap, fp)) != - 1) {
        while (lineLen > 0 && (line[lineLen - 1] == '\n' ||
                            line[lineLen - 1] == '\r'))
            --lineLen;
        editorAppendRow(line, lineLen);
    }
    free(line);
    fclose(fp);
}

/*** append buffer ***/

/* dynamic string struct */
typedef struct {
    char *b;
    int len;
} abuf;

#define ABUF_INIT {NULL, 0};

void abAppend(abuf *ab, const char *s, int len) {
    ab->b = realloc(ab->b, ab->len + len);
    memcpy(&ab->b[ab->len], s, len);
    ab->len += len;
}

void abFree(abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editorScroll() {
    E.rx = 0;
    if (E.cy < E.numRows) {
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }

    if (E.cy < E.rowOff) {
        E.rowOff = E.cy;
    }
    if (E.cy >= E.rowOff + E.screenRows) {
        E.rowOff = E.cy - E.screenRows + 1;
    }
    if (E.cx < E.colOff) {
        E.colOff = E.cx;
    }
    if (E.cx >= E.colOff + E.screenCols) {
        E.colOff = E.cx - E.screenCols + 1;
    }
}

void editorDrawRows(abuf *ab) {
    int y;
    for (y = 0; y < E.screenRows; ++y) {
        int fileRow = y + E.rowOff;
        if (fileRow >= E.numRows) {
            if (E.numRows == 0 && y == E.screenRows / 3) {
                char welcome[80];
                int welcomeLen = snprintf(welcome, sizeof(welcome),
                        "Kilo editor -- version %s", KILO_VERSION);
                if (welcomeLen > E.screenCols) welcomeLen = E.screenCols;
                int padding = (E.screenCols - welcomeLen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    --padding;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomeLen);
            }
            else {
                abAppend(ab, "~", 1);
            }
        }
        else {
            int len = E.row[fileRow].rsize - E.colOff;
            if (len < 0) len = 0;
            if (len > E.screenCols) len = E.screenCols;
            abAppend(ab, &E.row[fileRow].render[E.colOff], len);
        }

        abAppend(ab, "\x1b[K", 3); // clear line
        abAppend(ab, "\r\n", 2);
    }
}

void editorDrawStatusBar(abuf *ab) {
    abAppend(ab, "\x1b[7m", 4); // invert color
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines",
            E.filename ? E.filename : "[No Name]", E.numRows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d%d",
            E.cy + 1, E.numRows);
    if (len > E.screenCols) len = E.screenCols;
    abAppend(ab, status, len);
    while (len < E.screenCols) {
        if (E.screenCols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        }
        else {
            abAppend(ab, " ", 1);
            ++len;
        }
    }
    abAppend(ab, "\x1b[m", 3); // back to normal
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msgLen = strlen(E.statusmsg);
    if (msgLen > E.screenCols) msgLen = E.screenCols;
    if (msgLen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msgLen);
}

void editorRefreshScreen() {
    editorScroll();

    abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide cursor before refresh
    abAppend(&ab, "\x1b[H", 3); // cursor to top left

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowOff) + 1,
                                              (E.rx - E.colOff) + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show the cursor back

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** input ***/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                --E.cx;
            }
            else if (E.cy > 0) {
                --E.cy;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                ++E.cx;
            }
            else if (row && E.cx == row->size) {
                ++E.cy;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                --E.cy;
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numRows) {
                ++E.cy;
            }
            break;
    }
    row = (E.cy >= E.numRows) ? NULL : &E.row[E.cy];
    int rowLen = row ? row->size : 0;
    if (E.cx > rowLen) {
        E.cx = rowLen;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey();

    switch(c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;

        case HOME_KEY:
            E.cx = 0;
            break;
        
        case END_KEY:
            if (E.cy < E.numRows)
                E.cx = E.row[E.cy].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenRows;
                while (times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
                }
                break;
            }

        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowOff = 0;
    E.colOff = 0;
    E.numRows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1) die("getWindowSize");
    E.screenRows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if  (argc >= 2) {
        editorOpen(argv[1]);
    }

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
