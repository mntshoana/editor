#ifndef EDITOR_H
#define EDITOR_H

#include <stdio.h>
#include <ctype.h> // iscntrl()

#include <unistd.h>
#include <termios.h> // to turn of echo mode and canonical mode
#include <stdlib.h> // atexit()

#include <string.h> // memcpy
#include <errno.h>

#include <sys/ioctl.h> // get the terminal size

#include <time.h>
#include <stdarg.h>

// ctr + char maps to ASCII byte between 1 and 26
#define controlKey(c) c & 0x1f

/* V100 escape sequences */
// https://vt100.net/docs/vt100-ug/chapter3.html
// \x1b is the escape character, 27
// escape sequence, nmber of chars
#define CL_SCREEN_ALL          "\x1b[2J" , 4
#define CL_SCREEN_ABOVE_CURSOR "\x1b[1J" , 4
#define CL_SCREEN_BELOW_CURSOR "\x1b[0J" , 4
#define CL_LINE_RIGHT_OF_CURSOR "\x1b[K", 3
#define CL_LINE_LEFT_OF_CURSOR "\x1b[1K", 4
#define CL_LINE_ALL            "\x1b[2K", 4

#define CL_INVERT_COLOR "\x1b[7m", 4
#define CL_FMT_BOLD "\x1b[1m", 4
#define CL_FMT_UNDERSCORE "\x1b[4m", 4
#define CL_FMT_BLINK "\x1b[5m", 4
#define CL_FMT_CLEAR "\x1b[m", 3

#define REPOS_CURSOR_TOP_LEFT  "\x1b[H", 3
#define REPOS_CURSOR_BOTTOM_RIGHT "\x1b[999C\x1b[999B", 12
#define QUERRY_CURSOR_POS      "\x1b[6n", 4

#define HIDE_CURSOR "\x1b[?25l", 6
#define SHOW_CURSOR "\x1b[?25h", 6
// end of V100 escape sequences

#define TAB_SPACES 8
struct pos {
    int x, y;
}cursorPos; // x, y elements begin from 1:n (not zero based)
struct termios copyFlags;
int screenrows;
int screencols;
int rowOffset; // to update pos as user scrolls up or down
int colOffset; // to update pos as user scrolls left or right

struct outputBuffer {
  char *buf;
  int size;
};

int openedFileLines;
struct outputBuffer* fromOpenedFile, * toRenderToScreen;
char *filename;

char statusmsg[80];
time_t statusmsg_time;

void failExit(const char *s);
void turnOfFlags();
void reset();
void refresh();

void editorInit();
int getWindowSize(int *rows, int *cols);

void appendToBuffer(struct outputBuffer* out, const char* str, int len);
void appendreposCursorSequence(struct outputBuffer* out, int x, int y);
int terminalOut(const char *sequence, int count);

char readCharacter();
void repositionCursor();
void processKey();

void loadTitle(struct outputBuffer* oBuf);
void loadRows(struct outputBuffer* oBuf, int delta);
void loadStatusBar(struct outputBuffer* oBuf);
void loadStatusMessage(const char *fmt, ...);

void scroll();

void openFile(char* file);
void appendNewLine(char* stringLine, int readCount);
void updateBuffer(struct outputBuffer* dest, struct outputBuffer* src);


void insertIntoBuffer(struct outputBuffer* buf, int at, int c);
void insertChar(int character);

char* prepareToString(int *bufferLength);
#endif // EDITOR_H
