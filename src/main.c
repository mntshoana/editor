#include <stdio.h>
#include <unistd.h>
#include <termios.h> // to turn of echo mode
#include <stdlib.h> // atexits

struct termios copyFlags;
void reset(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &copyFlags);
}

void turnOfFlags() {
    // first turn of echo mode
    struct termios rawFlags;
    tcgetattr(STDIN_FILENO, &rawFlags);
    // but save original flags and restore them before exiting
    copyFlags = rawFlags;
    atexit(reset);
    // continue to turn of echo mode
    rawFlags.c_lflag &= ~(ECHO); // echo off
    // c_lflag: “local flags”
    // c_iflag: "input flags"
    // c_oflag: "output flags"
    // c_cflag: "control flags"
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawFlags);
    // TCSAFLUSH argument specifies when to apply the change: it waits for all pending output to be written to the terminal, and also discards any input that hasn’t been read.
}

int main () {
    // First turn of Echo mode
    turnOfFlags();
    
    printf("Welcome.Feel free to type. Type q to quit\n");
    char c;
    // still in cononical mode
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q')
            break;
    }
    return 0;
}
