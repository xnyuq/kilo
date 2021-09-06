/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>

/*** data ***/

/* termios struct from termios.h to manipulate terminal's atributes */
struct termios orig_termios;


/*** terminal ***/

/*error handling*/
void die(const char *s) {
     /*print s then exit with code 1*/
    perror(s);
    exit(1);
}

/* disable raw mode (return to canonical mode)*/
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

/* canonical mode: input are sent after '\n'*/
/* raw mode: read byte-by-byte instead of line-by-line, enabled by turn off many flags in the terminal*/
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) 
        die("tcgetattr");
     /*execute disableRawMode function when receive exit signal*/
    atexit(disableRawMode);
    
    struct termios raw = orig_termios;

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
    raw.c_cc[VMIN] = 0; // VMIN: min bytes needed before read() can return;
    raw.c_cc[VTIME] = 1; // max time to wait before read() return;

    /* apply to your terminal with tcsetattr and TCSAFLUSH */
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) 
        die("tcsetattr");
}


/*** init ***/

int main() {
    enableRawMode();

    while(1) {
        char c = '\0';
        if (read(STDERR_FILENO, &c, 1) == -1 &&errno != EAGAIN)
            die("read");
        /* check if c is escape sequence */
        if (iscntrl(c)) {
            /* with post processing disabled: \r\n for newline */
            printf("%d\r\n", c);
        }
        else {
            printf("%d ('%c')\r\n", c, c);
        }
        if (c == 'q') break;
    }

    return 0;
}
