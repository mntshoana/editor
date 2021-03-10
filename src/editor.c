#include "editor.h"

// Prints an error message and exits the program
void failExit(const char *s) {
    perror(s);
    exit(1);
}

// Terminal is adjusted to a custom state
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
    // ~INPCK (old, usually not important) disables parity checking, which doesn’t seem to                  ly to modern terminal emulators.
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

// Terminal is reset to it's natural state
void reset(){
    int res = tcsetattr(STDIN_FILENO, TCSAFLUSH, &copyFlags);
    if (res == -1)
        failExit("Could not reset flags");
}

void refresh(){
    struct outputBuffer oBuf = {NULL, 0};
    appendToBuffer(&oBuf, HIDE_CURSOR);
    appendToBuffer(&oBuf, CL_SCREEN_ALL);
    appendToBuffer(&oBuf, REPOS_CURSOR_TOP_LEFT);
    if (openedFileLines == 0) {
        loadTitle(&oBuf); // 1 row
        cursorPos.y = 2;
        loadRows(&oBuf, -1); // - 1 for title row
    }
    else loadRows(&oBuf, -1); // for an empty bottom row
        
    appendreposCursorSequence(&oBuf, cursorPos.x, cursorPos.y);
    appendToBuffer(&oBuf, SHOW_CURSOR);
    terminalOut(oBuf.buf, oBuf.size);
    free(oBuf.buf);
}

void loadTitle(struct outputBuffer* oBuf){
    const char* title = "Welcome. feel free to type."
                  " Press \"ctr+q\" to quit\r\n";
    int len = (strlen(title) > screencols) ? screencols : strlen(title);
    int paddingLen = (screencols - len ) / 2;
    char padding [paddingLen];
    for (int i = 0; i < paddingLen; i++)
        padding[i] = ' ';
    appendToBuffer(oBuf, padding, paddingLen);
    appendToBuffer(oBuf, title, len);
}
void loadRows(struct outputBuffer* oBuf, int delta){
    
    scroll(); // scroll updates rowOffset to position
    for (int y = 0; y < screenrows + delta; y++)
        if (y + rowOffset < openedFileLines) { // display file contents
            int pos = y + rowOffset;
            int len = (openedFile[pos].size > screencols) ? screencols : openedFile[pos].size;
            
            //char conv[5];
            //sprintf( conv, "%d", pos );
            //appendToBuffer(oBuf, conv, strlen(conv));

            appendToBuffer(oBuf, openedFile[pos].buf, len);
            appendToBuffer(oBuf, "\r\n", 2);
            if (y == screenrows + delta - 1
                && pos != openedFileLines -1)
                appendToBuffer(oBuf, "...", 3 );
        }
        else {
            appendToBuffer(oBuf, "~", 1);
            if (y < screenrows + delta - 1)
                appendToBuffer(oBuf, "\r\n", 2);
        }
            
}
void scroll() {
    // cursorPos.y is 1 based
    if (cursorPos.y < rowOffset && cursorPos.y == 0) {
        --rowOffset; // cursor is above window, need to scroll up
    }
    // rowOffset = top of screen
    // screenrows = size of screen
    else if (cursorPos.y >= rowOffset + screenrows){ // cursor is below window,  need to scroll down
        rowOffset = cursorPos.y - screenrows;
        //cursorPos.y -= 1; // return cursorPos to within screen range
    }
    repositionCursor();
}

void editorInit() {
    int res = getWindowSize(&screenrows, &screencols);
    if (res == -1)
        failExit("Could not get the editor / terminal size");
    
    // initialize rest of editor
    cursorPos.y =1;
    cursorPos.x =1;
    rowOffset = 0; // start by scrolling to the top
    openedFileLines = 0;
    openedFile = NULL;
}

/*
 * Retreives the size of the terminal's width and height
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    // ??maybe stands for: Terminal Input/Output Control) Get WINdow SiZe.)
    int res = ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    if (res == -1 || ws.ws_col == 0){
        // ioctl failed, try alternative method to get height and width
        struct outputBuffer oBuf = {NULL, 0};
        appendToBuffer(&oBuf, REPOS_CURSOR_BOTTOM_RIGHT);
        appendToBuffer(&oBuf, QUERRY_CURSOR_POS);
        terminalOut(oBuf.buf, oBuf.size);
        free(oBuf.buf);
        printf("\r\n");
        char buf[32];
        unsigned int i = 0;
        while (i < sizeof(buf) - 1) {
            if (read(STDIN_FILENO, &buf[i], 1) != 1)
                break; // error
            if (buf[i] == 'R') {
                buf[i] = '\0';
                break; // terminate input once R is reached
            }
            i++;
        }
        if (buf[0] != '\x1b' || buf[1] != '[')
            // Alternative method failed
            return -1;
        
        int res = sscanf(&buf[2], "%d;%d", rows, cols);
        if ( res != 2) // need to read both integers or
            // or else Alternative method fails
            return -1;
        
        return 0;
    }
    else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
    return 0;
}


/* appendToBuffer
 * Dynamically reallocates memory for outputting a string
 */
void appendToBuffer(struct outputBuffer* out, const char* str, int len) {
    if (len == 0)
        return;
    char* ptr = realloc(out->buf, out->size + len);
    if (ptr == NULL)
      return; // failed to reallocate

    memcpy( &ptr[out->size], str, len);
    out->buf = ptr;
    out->size += len;
}

// Helper function to conver cursor positions to terminal sequence string
void appendreposCursorSequence(struct outputBuffer* out, int x, int y) {
    char temp[32];
    snprintf(temp, sizeof(temp), "\x1b[%d;%dH", y, x);
    appendToBuffer(out, temp, strlen(temp));
}

/* termial
 * Makes it easeir to write to the terminal
 *   appends characters and escape sequences to the buffer
 */
 int terminalOut(const char *sequence, int count){
    return write(STDOUT_FILENO, sequence, count);
}

char readCharacter(){
    // read character
    char c = '\0';
    int res;
    while ((res = read( STDIN_FILENO, &c, 1)) != 1) {
        if (res == -1 && errno != EAGAIN)
            failExit("Unable to read input");
    }

    // Handle special keys
    if (c == '\x1b') {
        char seq[3];
        res = read(STDIN_FILENO, &seq[0], 1);
        if (res != 1) // if fails
            return c; // Assume key = ESC
        res = read(STDIN_FILENO, &seq[1], 1);
        if (res != 1) // if fails
            return c; // Assume key = ESC
        if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H':// Home key
                    cursorPos.x = 0;
                    repositionCursor();
                    break;
                case 'F': // End
                    cursorPos.x = screencols -1;
                    repositionCursor();
                    break;
            }
            return readCharacter();
        }
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                // Check for longer sequence, needs one more char
                res = read(STDIN_FILENO, &seq[2], 1);
                if (res != 1) // if fails
                    return c; // Assume key = ESC
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '1': case '7': // Home key
                            cursorPos.x = 0;
                            repositionCursor();
                            break;
                        case '4': case '8': // End key
                            cursorPos.x = screencols -1;
                            repositionCursor();
                            break;
                        case '3': // Delete
                            break;
                        case '5': // Page up
                            break;
                        case '6': // Page down
                            break;
                    }
                    return readCharacter();
                }
            }
            switch (seq[1]) {
              case 'A': // Arrow up
                if (cursorPos.y > 0) { // can never pass 0, allow overscreen by 1
                    cursorPos.y--;
                    if (cursorPos.y >= screenrows)
                        cursorPos.y = screenrows; // return cursorPos to within screen range
                    repositionCursor();
                }
                break;
              case 'B': // Arrow down
                if (cursorPos.y <= openedFileLines){ // can never pass max, allow overscreen by 1
                    cursorPos.y++;
                    repositionCursor();
                }
                break;
              case 'C': // Arrow right
                if (cursorPos.x < screencols){
                    cursorPos.x++;
                    repositionCursor();
                }
                break;
              case 'D': // Arrow left
                if (cursorPos.x > 1){
                  cursorPos.x--;
                  repositionCursor();
                }
                break;
              case 'H':{ // Home
                  cursorPos.x = 0;
                  repositionCursor();
                }
                break;
              case 'F': // End
                  cursorPos.x = screencols -1;
                  repositionCursor();
                  break;
            }
        }
        refresh();
        return readCharacter();
    }
    return c;
}

void repositionCursor(){
    struct outputBuffer oBuf = {NULL, 0};
    appendToBuffer(&oBuf, HIDE_CURSOR);
    appendreposCursorSequence(&oBuf, cursorPos.x, cursorPos.y);
    appendToBuffer(&oBuf, SHOW_CURSOR);
    terminalOut(oBuf.buf, oBuf.size);
    free(oBuf.buf);
}

// Key press event handler
void processKey(){
    char c = readCharacter();
    // process character
    switch (c) {
        case controlKey('q'):
            terminalOut(CL_SCREEN_ALL);
            terminalOut(REPOS_CURSOR_TOP_LEFT);
            for (int i = 0; i < openedFileLines; i++ )
                free(openedFile[i].buf);
            free(openedFile);
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

void openFile(char* file) {
    FILE* f = fopen(file, "r");
    if (!f)
        failExit("Could not open file");
    char *line = NULL;
    size_t size = 0;
    int readCount;
    while ((readCount = getline(&line, &size, f))!= -1) {
        while (readCount > 0 && (line[readCount - 1] == '\n'
                || line[readCount - 1] == '\r') )
            readCount--;
               
        openedFile = realloc(openedFile, sizeof(struct outputBuffer) * (openedFileLines+1 ));
        int i = openedFileLines;
        openedFile[i].size = readCount;
        openedFile[i].buf = malloc(readCount + 1);
        memcpy(openedFile[i].buf, line, readCount);
        openedFile[i].buf[readCount] = '\0';
        openedFileLines += 1;
    }
    free(line);
    fclose(f);
}
