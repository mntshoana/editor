#include <stdio.h>
#include <unistd.h>
#include <termios.h> // to turn of echo mode


int main () {
    // first turn of echo mode
    struct termios rawFlags;
    tcgetattr(STDIN_FILENO, &rawFlags);
    rawFlags.c_lflag &= ~(ECHO); // echo off
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &rawFlags);
    
    printf("Welcome.Feel free to type. Type q to quit\n");
    char c;
    // still in cononical mode
    while (read(STDIN_FILENO, &c, 1) == 1) {
        if (c == 'q')
            break;
    }
    return 0;
}
