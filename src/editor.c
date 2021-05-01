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
        cursorPos.x = 2;
        loadRows(&oBuf, -1); // - 1 for title row
    }
    else loadRows(&oBuf, 0); // for an empty bottom row
    
    loadStatusBar(&oBuf);
    appendreposCursorSequence(&oBuf, cursorPos.x, cursorPos.y);
    appendToBuffer(&oBuf, SHOW_CURSOR);
    terminalOut(oBuf.buf, oBuf.size);
    free(oBuf.buf);
}

void editorInit() {
    int res = getWindowSize(&screenrows, &screencols);
    if (res == -1)
        failExit("Could not get the editor / terminal size");
    else
        screenrows -= 2; // make room for the status bar
    // initialize rest of editor
    cursorPos.y =1;
    cursorPos.x =1;
    rowOffset = 0; // represents an offset from to the top of 0
    colOffset = 0; // represents an offset from the left of 0
    openedFileLines = 0;
    fromOpenedFile = NULL;
    toRenderToScreen = NULL;
    
    filename = NULL;
    fileModified = 0;
    
    statusmsg[0] = '\0';
    statusmsg_time = 0;
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

    // Allocate memory
    char* ptr = realloc(out->buf, out->size + len );
    if (ptr == NULL)
      return; // failed to reallocate
    
    // Append
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
            refresh();
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
                            cursorPos.x = 1;
                            break;
                        case '4': case '8': // End key
                            if (cursorPos.y + rowOffset < openedFileLines && fromOpenedFile)
                                cursorPos.x = toRenderToScreen[cursorPos.y + rowOffset -1].size;
                                
                            break;
                            
                        case '3': // Delete
                            deleteChar();
                            break;
 
                        case '5': // Page up
                            rowOffset -= screenrows;
                            if (rowOffset < 0)
                                rowOffset = 0;
                            break;
                        case '6': // Page down
                            rowOffset += screenrows - 1;
                            if (rowOffset + cursorPos.y > openedFileLines)
                                rowOffset = openedFileLines - cursorPos.y;
                            break;
                    }
                    // Consider tabs
                    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
                    cursorPos.x = addTabs(line, cursorPos.x );
                    
                    // Snap to end of line
                    if (cursorPos.y < screenrows && fromOpenedFile) {
                      int currentRowEnd = toRenderToScreen[cursorPos.y - 1 + rowOffset].size + 1;
                      if (cursorPos.x > currentRowEnd)
                          cursorPos.x = currentRowEnd;
                    }
                    refresh();
                    return readCharacter();
                }
            }
            switch (seq[1]) {
              case 'A': // Arrow up
                if (cursorPos.y > 0) { // can never pass 0, allow overscreen by 1
                    cursorPos.y--;
                }
                break;
              case 'B': // Arrow down
                if (cursorPos.y <= screenrows){ // can never pass max, allow overscreen by 1
                    cursorPos.y++;
                }
                    
                break;
              case 'C': // Arrow right
                if (cursorPos.y <= screenrows
                    && fromOpenedFile){
                    if (cursorPos.x <= toRenderToScreen[cursorPos.y + rowOffset - 1].size){
                        cursorPos.x++;
                    }
                    else if (cursorPos.y < screenrows &&
                    cursorPos.x == toRenderToScreen[cursorPos.y + rowOffset -1].size + 1){
                        cursorPos.y++;
                        cursorPos.x = 1;
                        colOffset = 0;
                    }
                    
                }
                break;
              case 'D': // Arrow left
                if (cursorPos.x > 1){
                    // Consider tabs
                    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
                    cursorPos.x = subtractTabs(line, cursorPos.x);
                    cursorPos.x = addTabs(line, cursorPos.x - 1);
                    
                    if (cursorPos.x >= screencols){
                        cursorPos.x = screencols; // return cursorPos to within screen range
                    }
                }
                else if (cursorPos.y > 1 && fromOpenedFile) { // move up to the end of the previous line
                    cursorPos.y--;
                    if (cursorPos.y > screenrows)
                        cursorPos.y = screenrows-1; // return cursorPos to within screen range
                    cursorPos.x = toRenderToScreen[cursorPos.y + rowOffset -1].size + 1;
                    // Consider tabs
                    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
                    cursorPos.x = addTabs(line, cursorPos.x );
                }
                break;
              case 'H':{ // Home
                    cursorPos.x = 1;
                }
                break;
              case 'F': // End
                    if (cursorPos.y < openedFileLines && fromOpenedFile)
                        cursorPos.x = toRenderToScreen[cursorPos.y + rowOffset -1].size + 1;
                  break;

            }
        }
        
        // Snap to end of line
        if (cursorPos.y > 0 && cursorPos.y <= screenrows +1 && fromOpenedFile) {
          int currentRowEnd = toRenderToScreen[cursorPos.y + rowOffset -1].size + 1;
          if (cursorPos.x > currentRowEnd)
              cursorPos.x = currentRowEnd;
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
    static int quit_conf = 1;
    char c = readCharacter();
    // process character
    switch (c) {
        case controlKey('q'):
            if (fileModified && quit_conf > 0){
                loadStatusMessage("Alert!!! There are unsaved changes. "
                                  "Save using ctrl+s "
                                  "or quit with ctrl+q again");
                quit_conf--;
                return; // so the count doesn't reset yet at the end of this function
            }
            terminalOut(CL_SCREEN_ALL);
            terminalOut(REPOS_CURSOR_TOP_LEFT);
            for (int i = 0; i < openedFileLines; i++ ){
                free(fromOpenedFile[i].buf);
                free(toRenderToScreen[i].buf);
            }
            free(fromOpenedFile);
            free(toRenderToScreen);
            free(filename);
            exit(0);
            break;
        
        case controlKey('s'):
            saveFile();
            break;
            
        case controlKey('f'):
            search();
            break;
            
        case '\r':            // Enter
            insertLine();
            break;
            
        case 127:             //  127 or Backspace key
        case controlKey('h'):   // traditionally used to find and replace, bet we use to delete
            deleteChar();
            break;
            
        case controlKey('l'):   // traditionally used to refresh the screen
            // Do nothing
        case '\x1b':            // Escape key
            // Do nothing
            break;
        default:
            insertChar(c);
            break;
    };
    printf("%d\r\n", c);
    quit_conf = 1;
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
    for (int y = 0; y <= screenrows + delta - 1; y++)
        if (y + rowOffset < openedFileLines) { // display file contents
            int pos = y + rowOffset;
            int len = (fromOpenedFile[pos].size  - colOffset > screencols)
                        ? (screencols)  :  (fromOpenedFile[pos].size - colOffset);
            
            if (len > 0)
                appendToBuffer(oBuf, fromOpenedFile[pos].buf + colOffset, len);

            appendToBuffer(oBuf, "\r\n", 2);
             
        }
        else {
            if (y < screenrows + delta){
                appendToBuffer(oBuf, "~", 1);
                appendToBuffer(oBuf, "\r\n", 2);
            }
        }
            
}

void loadStatusBar(struct outputBuffer* oBuf){
    appendToBuffer(oBuf, CL_INVERT_COLOR);
    char status[80], rstatus[80];
    const char* modifiedStatus = fileModified ? "*modified": "";
    int width = snprintf(status, sizeof(status),
                         "%.20s - %d lines %s",
                         filename
                         ? filename : "[Unsaved File]", openedFileLines, modifiedStatus );
    int rwidth = snprintf(rstatus, sizeof(rstatus),
                          "%d/%d",
                          cursorPos.y + rowOffset, openedFileLines);
    if (width > screencols)
        width = screencols;
    appendToBuffer(oBuf, status, width);
    for (; width < screencols; width++){
        if (screencols - width == rwidth) {
            appendToBuffer(oBuf, rstatus, rwidth);
            break;
        }
        else
            appendToBuffer(oBuf, " ", 1);
    }
    appendToBuffer(oBuf, CL_FMT_CLEAR);
    appendToBuffer(oBuf, "\r\n", 2);
    
    // Next Line: status message
    appendToBuffer(oBuf, CL_LINE_RIGHT_OF_CURSOR); // clear the status message
    int msgSize = strlen(statusmsg);
    if (msgSize > screencols)
        msgSize = screencols;
    if (msgSize
        && time(NULL) - statusmsg_time < 7)// display message (for 7 seconds)
        appendToBuffer(oBuf, statusmsg, msgSize);
}

void loadStatusMessage(const char *fmt, ...){
    va_list additionalArgs;
    va_start(additionalArgs, fmt);
    vsnprintf(statusmsg, sizeof(statusmsg), fmt, additionalArgs);
    va_end(additionalArgs);
    statusmsg_time = time(NULL);
}

char* userPrompt(char* message){
    size_t inputSize = 128;
    char* input = malloc(inputSize);
    input[0] = '\0';
    
    size_t len = 0;
    
    while (1){
        loadStatusMessage(message, input);
        refresh(); // allows to see input on screen
        
        int charIn = readCharacter();
        if (charIn == '\x1b'){  // Escape key
            loadStatusMessage("");
            free(input);
            return NULL;
        }
        else if ( charIn == (controlKey('h') )|| charIn == 127){ // Backspace
            if (len != 0){
                len--;
                input[len] = '\0';
            }
        }
        else if (charIn == '\r'){
            // exit prompt
            if (len != 0) {
                loadStatusMessage("");
                return input;
            }
        }
        else if (!iscntrl(charIn) && charIn < 128){
            if (len == inputSize - 1){
                inputSize *= 2;
                input = realloc(input, inputSize);
            }
            input[len++] = charIn;
            input[len] = '\0';
        }
    }
}

void scroll() {
    // VERTICAL SCROLLING
    // cursorPos.x and y are 1 based
    // rowOffset = top of screen
    // screenrows = size of screen
    
    // Up
    if (cursorPos.y < rowOffset && cursorPos.y == 0) {
        --rowOffset; // cursor is above window, need to scroll up
    }
    if (cursorPos.y <= 1)
        cursorPos.y = 1; // return cursorPos to within screen range, deliberately skip 1 for visual smoothness of scrolling
    
    // Down
    else if (cursorPos.y == screenrows +1 && cursorPos.y + rowOffset <= openedFileLines){ // cursor is below window,  need to scroll down
        rowOffset++;
        //cursorPos.y -= 1; // return cursorPos to within screen range
    }
    if (cursorPos.y > screenrows)
        cursorPos.y = screenrows; // return cursorPos to within screen range
    
    // Consider tabs
    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
    cursorPos.x = addTabs(line, cursorPos.x );
    
    // HORIZONTAL SCROLLING
    if (cursorPos.x < colOffset && cursorPos.x == 0) {
        --colOffset;
     }
     if (cursorPos.x >= colOffset + screencols) {
       colOffset = cursorPos.x - screencols + 1;
     }
    repositionCursor();
}

int zeroTabs(struct outputBuffer* line, int* xPos){
    int i = 0;
    
    int idx = 0;
    while ( idx < *xPos - 1) {
        if (line->buf[i] == '\t') {
            idx++;
            while (idx % TAB_SPACES != 0)
                idx++;
        }
        else
            idx++;
        // End of loop
        i++; // increment
    }

    *xPos = i + 1;
    return idx + 1;;
}

int addTabs(struct outputBuffer* line, int xPos){
    int index = zeroTabs(line, &xPos);
    return index;
}
int subtractTabs(struct outputBuffer* line, int xPos){
    int index = zeroTabs(line, &xPos);
    return xPos;
}

void openFile(char* file) {
    free(filename);
    filename = strdup(file);
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
               
        insertNewLine(openedFileLines,  line, readCount);
    }
    free(line);
    fclose(f);
    fileModified = 0;
}

void updateBuffer(struct outputBuffer* dest, struct outputBuffer* src){
    // Searching for tabs
    int tabs = 0;
    for (int i = 0; i < src->size; i++)
        if (src->buf[i] == '\t')
            tabs++;
    
    // allocate extra space
    free(dest->buf);
    dest->buf = malloc(src->size + tabs * (TAB_SPACES - 1) + 1); // + 1 is to make space for null
    
    // Append
    if (tabs == 0){
        memcpy( dest->buf, src->buf, src->size); // no tabs to render
        dest->buf[src->size] = '\0';
        dest->size = src->size;
    }
    else {
        int idx = 0;
        for (int i = 0; i < src->size; i++) {
            if (src->buf[i] == '\t') {
                dest->buf[idx++] = ' ';
                while (idx % TAB_SPACES != 0)
                    dest->buf[idx++] = ' ';
            } else
                dest->buf[idx++] = src->buf[i];
        }
        dest->buf[idx] = '\0';
        dest->size = idx;
    }
}

// Adds a string to the end of the output buffer and the render buffer
//  when opening a file or typing into the editor
void insertNewLine(int at, char* stringLine, int readCount){
    if (at < 0 || at > openedFileLines)
        return;
    
    // Reallocate memory to allow for enough space
    fromOpenedFile = realloc(fromOpenedFile, sizeof(struct outputBuffer) * (openedFileLines + 1));
    // shift contents to make room for insertion
    if (at +1 < openedFileLines){
        memmove(&fromOpenedFile[at+1], &fromOpenedFile[at],
                sizeof(struct outputBuffer) * (openedFileLines - at));
    }
    
    fromOpenedFile[at].size = readCount;
    fromOpenedFile[at].buf = malloc(readCount + 1);
    memcpy(fromOpenedFile[at].buf, stringLine, readCount);
    fromOpenedFile[at].buf[readCount] = '\0';
    
    // Render tabs properly
    toRenderToScreen = realloc(toRenderToScreen, sizeof(struct outputBuffer) * (openedFileLines + 1));
    toRenderToScreen[at].size = 0;
    toRenderToScreen[at].buf = NULL;
    updateBuffer(&toRenderToScreen[at], &fromOpenedFile[at]);
    
    openedFileLines += 1;
    fileModified += 1;
}

void appendString(struct outputBuffer* source, int line, char* string, size_t len){
    source[line].buf = realloc(source[line].buf, source[line].size + len +1);
    memcpy(&source[line].buf[source[line].size], string, len);
    source[line].size += len;
    source[line].buf[source[line].size] = '\0';
    updateBuffer(&toRenderToScreen[line], &fromOpenedFile[line]);
    fileModified += 1;
}



void insertIntoBuffer(struct outputBuffer* dest, int pos, int c){
    if (pos < 0 || pos > dest->size)
        pos = dest->size; // if not within the bounds of the existing line
    
    // alocate memory for two more characters
    dest->buf = realloc(dest->buf, dest->size + 2);
    // move substring to make room for a single character
    if (dest->size) // line not empty
        memmove(&dest->buf[pos + 1], &dest->buf[pos], (dest->size) - pos  );
    // update buffer with new character
    dest->buf[pos] = c;
    dest->size++;
    dest->buf[dest->size] = '\0';
    fileModified += 1;
}

void insertChar(int character) {
    int yPos = cursorPos.y + rowOffset - 1;
    int xPos = cursorPos.x + colOffset - 1;
    
    if (!fromOpenedFile){
        // Currently on the line after the title
        // because no file is open
        cursorPos.y = 1;
        cursorPos.x = 1;
        xPos = 0;
        yPos = 0;
        insertNewLine(openedFileLines, "", 0);
    }
    else if ( yPos == openedFileLines) { // buffer too small
        insertNewLine(openedFileLines, "", 0);
    }
    insertIntoBuffer(&fromOpenedFile[yPos], xPos, character);
    updateBuffer(&toRenderToScreen[yPos], &fromOpenedFile[yPos]);
    cursorPos.x++;
}

void insertLine(){
    // when pressing enter
    int yPos = cursorPos.y + rowOffset - 1;
    int xPos = cursorPos.x + colOffset - 1;
    if (xPos == 0){ // Add an empty line
        insertNewLine(yPos, "", 0);
    }
    else{
        struct outputBuffer *ref = &fromOpenedFile[yPos];
        insertNewLine(yPos + 1, &ref->buf[xPos], ref->size - xPos);
        
        ref = &fromOpenedFile[yPos];
        ref->size = xPos;
        ref->buf[ref->size] = '\0';

        updateBuffer(&toRenderToScreen[yPos], &fromOpenedFile[yPos]);
    }
    cursorPos.y++;
    cursorPos.x = 1;
    
}
void deleteFromBuffer(struct outputBuffer* dest, int at){
    if (at < 0 || at > dest->size)
        return; // if not within the bounds of the existing line
    
    memmove(&dest->buf[at], &dest->buf[at +1], dest->size - at);
    dest->size--;
    fileModified += 1;
}

void deleteChar(){
    if (!fromOpenedFile){
        return;
    }
    else {
        // Consider tabs
        cursorPos.x = subtractTabs(&fromOpenedFile[cursorPos.y + rowOffset - 1], cursorPos.x );
        
        int yPos = cursorPos.y + rowOffset - 1;
        int xPos = cursorPos.x + colOffset - 1;
        if (xPos > 0){
            deleteFromBuffer(&fromOpenedFile[yPos], xPos -1);
            cursorPos.x--;
            updateBuffer(&toRenderToScreen[yPos], &fromOpenedFile[yPos]);
        }
        else if (xPos == 0 && yPos > 0){
            cursorPos.x = fromOpenedFile[yPos - 1].size + 1;
            cursorPos.y--;
            appendString(fromOpenedFile, yPos - 1, fromOpenedFile[yPos].buf, fromOpenedFile[yPos].size);
            deleteRow(yPos);
        }
        cursorPos.x = addTabs(&fromOpenedFile[cursorPos.y + rowOffset - 1], cursorPos.x );
    }
}

void deleteRow(int at){
    if (at < 0 || at >= openedFileLines)
        return;
    
    free(fromOpenedFile[at].buf);
    free(toRenderToScreen[at].buf);
    
    memmove(&fromOpenedFile[at], &fromOpenedFile[at + 1], sizeof(struct outputBuffer) * (openedFileLines - at -1) );
    memmove(&toRenderToScreen[at], &toRenderToScreen[at + 1], sizeof(struct outputBuffer) * (openedFileLines - at -1) );
    
    openedFileLines--;
    fileModified++;
}

char* prepareToString(int *bufferLength){
    int stringLength = 0;
    for (int i = 0; i < openedFileLines; i++ )
        stringLength += fromOpenedFile[i].size + 1; // add one to make room for the new line character
    *bufferLength = stringLength;
    
    char* preparedString = (char*) malloc(stringLength);
    char* iter = preparedString;
    for (int i = 0; i < openedFileLines; i++){
        memcpy(iter, fromOpenedFile[i].buf, fromOpenedFile[i].size);
        iter += fromOpenedFile[i].size;
        *iter = '\n';
        iter++;
    }
    return preparedString; // note, caller should free this
}

void saveFile(){
    if (filename == NULL){
        filename = userPrompt("Save as : %s");
        // Status bar uses formated strings
        if (filename == NULL){
            loadStatusMessage("Save aborted.");
            return;
        }
            
    }
    
    int len;
    char *string = prepareToString(&len);
    int descriptor = open(filename , O_RDWR | O_CREAT, 0644); // Owner permission read + write, others only read
    if (descriptor != -1){
        int res = ftruncate(descriptor, len);
        if (res != -1){
            res = write(descriptor, string, len);
            if (res == len){
                close(descriptor);
                free(string);
                fileModified = 0;
                loadStatusMessage("Saved! %d bytes written to disk", len);
                return;
            }
        }
        close(descriptor);
    }
    free(string);
    loadStatusMessage("Error! Cannot save - I/O error details: %s", strerror(errno));
}

void search(){
    char* query = userPrompt("Search: %s (ESC to cancel)");
    if (query == NULL)
        return;
    
    for (int i = 0; i < openedFileLines; i++){
        struct outputBuffer* line = &toRenderToScreen[i];
        char* match = strstr(line->buf, query);
        if (match){
            cursorPos.y = i+1;
            cursorPos.x = match - line->buf +1;
            if (cursorPos.y > screenrows)
                rowOffset = cursorPos.y - screenrows -1;
            else
                rowOffset = cursorPos.y - 1;
            cursorPos.y = 1;
            break;
        }
    }
    free(query);
}
