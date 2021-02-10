#include <stdio.h>
#include <ctype.h> // iscntrl()

#include <unistd.h>
#include <termios.h> // to turn of echo mode and canonical mode
#include <stdlib.h> // atexit()

struct termios copyFlags;
void reset(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &copyFlags);
}

void turnOfFlags() {
    struct termios rawFlags;
    tcgetattr(STDIN_FILENO, &rawFlags);
    // Save original flags to restore them before exiting
    copyFlags = rawFlags;
    atexit(reset);
    
    // c_lflag: “local flags"  // c_iflag: "input flags"
    // c_oflag: "output flags" // c_cflag: "control flags"
    rawFlags.c_lflag &= ~(ECHO | ICANON | | IEXTEN | ISIG);
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
    
    rawFlags.c_cc[VMIN] = 0; // minimum number of bytes needed before read() can return
    rawFlags.c_cc[VTIME] = 1; // maximum amount of time to wait before read() returns.
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawFlags);
    // TCSAFLUSH argument specifies when to apply the change: it waits for all pending output to be written to the terminal, and also discards any input that hasn’t been read.
}

int main () {
    // First turn of Echo mode and canonical mode
    turnOfFlags();
    
    printf("Welcome.Feel free to type. Type q to quit\n");
    char c = '\0';
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q')
            break;
        // test raw mode
        // remember ASCII 0–31 and 127 are control characters
        //                32–126 are all printable.
        // ctr + s == to stop sending output
        // ctr + q == resume sending output
        // ctr + z (or y) suspends programm to the background.
        // Run the fg command to resume
        	
        if (iscntrl(c)) {
              printf("%d\n", c);
            } else {
              printf("%d ('%c')\r\n", c, c);
            }
    }
    return 0;
}