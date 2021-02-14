#include <stdio.h>
#include <ctype.h> // iscntrl()

#include <unistd.h>
#include <termios.h> // to turn of echo mode and canonical mode
#include <stdlib.h> // atexit()

#include <errno.h>

#include <sys/ioctl.h> // get the terminal size

struct termios copyFlags;
int screenrows;
int screencols;

// ctr + c maps to ASCII byte between 1 and 26
#define controlKey(c) c & 0x1f

/* V100 escape sequences */
// https://vt100.net/docs/vt100-ug/chapter3.html
// \x1b is the escape character, 27
// escape sequence, nmber of chars
#define CL_SCREEN_ALL          "\x1b[2J" , 4
#define CL_SCREEN_ABOVE_CURSOR "\x1b[1J" , 4
#define CL_SCREEN_BELOW_CURSOR "\x1b[0J" , 4
#define REPOS_CURSOR_TOP_LEFT  "\x1b[H", 3

void terminalEscape(const char *sequence, int count){
    write(STDOUT_FILENO, sequence, count);
}
void failExit(const char *s) {
    perror(s);
    exit(1);
}

void reset(){
    int res = tcsetattr(STDIN_FILENO, TCSAFLUSH, &copyFlags);
    if (res == -1)
        failExit("Could not reset flags");
}

void turnOfFlags() {
    struct termios rawFlags;
    int res;
    res = tcgetattr(STDIN_FILENO, &rawFlags);
    if (res == -1)
        failExit("Could not retreive flags");
    // Save original flags to restore them before exiting
    copyFlags = rawFlags;
    atexit(reset);
    
    // c_lflag: “local flags"  // c_iflag: "input flags"
    // c_oflag: "output flags" // c_cflag: "control flags"
    rawFlags.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    // ~ECHO echo mode off
    // ~ICANON canonical mode off - reads byte by byte, not line by line
    // ~ISIG - reads ctr + c not as (SIGINT) and ctr + z not as (SIGTSTP) and ctr + y not to suspend to background
    // ~IEXTEN - ctr + v not to have the terminal wait for you to type another character
    //         - ctr + o not to discard the control character
    rawFlags.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // ~BRKINT (old, usually not important) not have a break condition cause a SIGINT signal
    // ~ICRNL - not to have the terminal helpfully translating any carriage returns into newlines (10, '\n').
    // ~INPCK (old, usually not important) disables parity checking, which doesn’t seem to apply to modern terminal emulators.
    // ~ISTRIP (old, usually turned off already) not to strip 8th bits of each input byte.
    // ~IXON - reads ctr + s and ctrl + q, usually they toggling data from being/not being transmitted to the terminal, for XON and XOFF of transmissions.
    
    
    rawFlags.c_oflag &= ~(OPOST);
    //  ~OPOST - not to translate "\n" to "\r\n". terminal requires both of these characters in order to start a new line of text.
    
    rawFlags.c_cflag |= (CS8);
    // |CS8 (a mask) sets the character size to 8 bits per byte. It is usually already set that way.
    
    // c_cc: control characters
    rawFlags.c_cc[VMIN] = 0; // minimum number of bytes needed before read() can return
    rawFlags.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns.  1/10th of a second
    res = tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawFlags);
    // TCSAFLUSH argument specifies when to apply the change: it waits for all pending output to be written to the terminal, and also discards any input that hasn’t been read.
    if (res == -1)
        failExit("Could not set flags (raw mode)");
}

// Key press event handler
void processKey(){
    // read character
    char c = '\0';
    int res;
    while ((res = read( STDIN_FILENO, &c, 1)) != 1) {
        if (res == -1 && errno != EAGAIN)
            failExit("Unable to read input");
    }
    // process character
    switch (c) {
        case controlKey('q'):
            exit(0);
            break;
        default:
            // Print
            // remember ASCII 0–31 and 127 are control characters
            //                32–126 are all printable.
            if (iscntrl(c))
                printf("%d\r\n", c);
            else
                printf("%d ('%c')\r\n", c, c);
    };
}

// Not sure if the following will function supports Windows OS
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    // ??maybe stands for Input/Output Control) Get WINdow SiZe.)
    int res = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    if (res == -1 || ws.ws_col == 0)
        return -1;
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
    return 0;
}

void editorSize() {
    int res = getWindowSize(&screenrows, &screencols);
    if (res== -1)
        failExit("Could not get the editor / terminal size");
}

int main () {
    // First turn of Echo mode and canonical mode
    turnOfFlags();
    editorSize(); // get the editor size

    
    printf("Welcome.Feel free to type. [ctr+q] to quit\r\n");
    terminalEscape(CL_SCREEN_ALL);
    terminalEscape(REPOS_CURSOR_TOP_LEFT);
    while (1) {
        processKey();
    }
    return 0;
}
