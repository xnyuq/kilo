/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

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

struct editorConfig {
    int cx, cy;
    int screenRows;
    int screenCols;
    /* termios struct from termios.h to manipulate terminal's atributes */
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/*error handling*/
void die(const char *s) {
    write(STDIN_FILENO, "\x1b[2J", 4);
    write(STDIN_FILENO, "\x1b[H", 3);

    /*print s then exit with code 1*/
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
    
    if (c == '\x1b') {
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
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
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
        /* move cursor 999 right and 999 down */
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
        return getCursorPosition(rows, cols);
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** append buffer ***/

/* dynamic string struct */
struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0};

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len);
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/*** output ***/

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenRows; ++y) {
        if (y == E.screenRows / 3) {
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

        abAppend(ab, "\x1b[K", 3); // clear line

        if (y < E.screenRows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6); // hide cursor before refresh
    abAppend(&ab, "\x1b[H", 3); // cursor to top left

    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // show the cursor back

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                --E.cx;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screenCols - 1) {
                ++E.cx;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                --E.cy;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenRows - 1) {
                ++E.cy;
            }
            break;
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
            E.cx = E.screenCols - 1;
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
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while(1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
