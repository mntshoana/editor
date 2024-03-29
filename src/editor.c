#include "editor.h"

// internal types to recognize
char * c_fam[] = {".c", ".h", ".cpp", NULL};
char * c_keyw[] = {"switch", "if", "while", "do", "for", "break", "continue", "return", "else", "enum", "struct", "union", "typedef", "register", "extern" ,"static", "class", "case", "volatile", "default",  "goto", "const|", "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "auto|", NULL};

char * text[] = {".txt", ".inf", NULL};
struct editorFlags database[] = {
        {
            "C",
            c_fam,
            c_keyw,
            highlight_num | highlight_string | highlight_comment
            | highlight_keyword_strong | highlight_keyword_regular
        }, // C Family
        {
            "Text file",
            text,
            NULL,
            normal
        }, // Text file
};
int databaseSize = sizeof(database) / sizeof(database[0]);

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

// updates the screen. Rewrites the output from the output buffer
void refresh(){
    struct outputBuffer oBuf = {NULL, 0, NULL};
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

// Initializes the editor with default values
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
    openedFileFlags = NULL;
    
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
        struct outputBuffer oBuf = {NULL, 0, NULL};
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
 * Dynamically reallocates memory for outputting a string to the screen
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

// Outputs a string the screen using a different color
void appendWithColor(struct outputBuffer* oBuf, const char* str, int len, int value){
    switch (value){
        case highlight_match:
            appendToBuffer(oBuf, CL_BLUE_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case highlight_comment:
            appendToBuffer(oBuf, CL_YELLOW_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case highlight_num:
            appendToBuffer(oBuf, CL_CYAN_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case highlight_keyword_strong:
            appendToBuffer(oBuf, CL_BLUE_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case highlight_keyword_regular:
            appendToBuffer(oBuf, CL_MAGENTA_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case highlight_string:
            appendToBuffer(oBuf, CL_RED_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
        case normal:
        default:
            appendToBuffer(oBuf, CL_DEFAULT_COLOR);
            appendToBuffer(oBuf, str, len);
            break;
    }
}

// Helper function to convert cursor positions to terminal sequence string
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


// A function to read a single character from the user and process it
char readCharacter(){
    // read character
    char c = '\0';
    int res;
    while ((res = read( STDIN_FILENO, &c, 1)) != 1) { // read one byte at a time
        if (res == -1 && errno != EAGAIN) // EAGAIN : no data available now, try again
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
                    colOffset = 0;
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
                    cursorPos.x = addTabs(line, cursorPos.x + colOffset );
                    
                    // Snap to end of line
                    if (cursorPos.y < screenrows + 1 && fromOpenedFile) {
                      int currentRowEnd = toRenderToScreen[cursorPos.y - 1 + rowOffset].size + 1;
                      if (cursorPos.x + colOffset > currentRowEnd){
                          cursorPos.x = currentRowEnd ;
                          colOffset = 0;
                        }
                    }
                    
                    refresh();
                    return readCharacter();
                }
            }
            switch (seq[1]) {
              case 'A': // Arrow up
                if (awaitingArrow){
                    lastArrow = 3;
                    break;
                }
                if (cursorPos.y > 0) { // can never pass 0, allow overscreen by 1
                    cursorPos.y--;
                }
                break;
              case 'B': // Arrow down
                if (awaitingArrow){
                    lastArrow = 4;
                    break;
                }
                if (cursorPos.y <= screenrows && rowOffset + cursorPos.y < openedFileLines ){ // can never pass max, allow overscreen by 1
                    if (cursorPos.y == screenrows)
                        rowOffset++;
                    else
                        cursorPos.y++;
                }
                break;
              case 'C': // Arrow right
                if (awaitingArrow){
                    lastArrow = 2;
                    break;
                }
                if (cursorPos.y <= screenrows
                    && fromOpenedFile){
                    if (cursorPos.x + colOffset < toRenderToScreen[cursorPos.y + rowOffset - 1].size + 1){
                        if (cursorPos.x < screencols)
                            cursorPos.x++;
                        else
                            colOffset++;
                    }
                    else if (cursorPos.y < screenrows &&
                    cursorPos.x + colOffset >= toRenderToScreen[cursorPos.y + rowOffset -1].size + 1){
                        cursorPos.y++;
                        cursorPos.x = 1;
                        colOffset = 0;
                    }
                }
                break;
              case 'D': // Arrow left
                if (awaitingArrow){
                    lastArrow = 1;
                    break;
                }
                if (cursorPos.x > 1){
                    // Consider tabs
                    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
                    cursorPos.x = subtractTabs(line, cursorPos.x);
                    cursorPos.x--;
                    cursorPos.x = addTabs(line, cursorPos.x);
                    if (cursorPos.x >= screencols)
                        cursorPos.x = screencols - 1; // return cursorPos to within screen range
                }
                else if (cursorPos.y > 1 && fromOpenedFile) { // move up to the end of the previous line
                    cursorPos.y--;
                    if (cursorPos.y > screenrows)
                        cursorPos.y = screenrows-1; // return cursorPos to within screen range
                    cursorPos.x = toRenderToScreen[cursorPos.y + rowOffset -1].size + 1;
                    // Consider tabs
                    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
                    cursorPos.x = addTabs(line, cursorPos.x);
                }
                else if (colOffset > 0)
                    colOffset--;
                
                break;
              case 'H':{ // Home
                    cursorPos.x = 1;
                    colOffset = 0;
                }
                break;
              case 'F': // End
                    if (cursorPos.y < openedFileLines && fromOpenedFile)
                        cursorPos.x = toRenderToScreen[cursorPos.y + rowOffset -1].size + 1;
                  break;

            }
        }
        
        // Snap to end of line
        if (cursorPos.y > 0 && cursorPos.y <= screenrows +1 && fromOpenedFile && cursorPos.y + rowOffset < openedFileLines) {
          int currentRowEnd = toRenderToScreen[cursorPos.y + rowOffset -1].size  + 1;
            if (cursorPos.x + colOffset > currentRowEnd){
              cursorPos.x = currentRowEnd ;
              colOffset = 0;
            }
        }
        refresh();
        if (lastArrow != 0)
            return 0;
        else
            return readCharacter();
    }
    return c;
}

// Updates the location of the blinking cursor
void repositionCursor(){
    struct outputBuffer oBuf = {NULL, 0, NULL};
    appendToBuffer(&oBuf, HIDE_CURSOR);
    appendreposCursorSequence(&oBuf, cursorPos.x, cursorPos.y);
    appendToBuffer(&oBuf, SHOW_CURSOR);
    terminalOut(oBuf.buf, oBuf.size);
    free(oBuf.buf);
}

// Processes the input keys (key press event handler)
void processKey(){
    static int quit_conf = 1;
    char c = readCharacter(); // input character
    switch (c) {
        case controlKey('q'): // quit
            if (fileModified && quit_conf > 0){
                loadStatusMessage("Alert!!! There are unsaved changes. "
                                  "Save using ctrl+s "
                                  "or quit with ctrl+q again");
                quit_conf--;
                return; // allow for a confirmation message. Repeat the action again to quit
            }
            // Begin clean up
            terminalOut(CL_SCREEN_ALL);
            terminalOut(REPOS_CURSOR_TOP_LEFT);
            for (int i = 0; i < openedFileLines; i++ ){
                free(fromOpenedFile[i].buf);
                free(toRenderToScreen[i].buf);
                free(toRenderToScreen[i].state);
            }
            free(fromOpenedFile);
            free(toRenderToScreen);
            
            free(filename);
            exit(0); // return will not exit the application
            break;
        
        case controlKey('s'): // save
            saveFile();
            break;
            
        case controlKey('f'): // find
            search();
            break;
            
        case '\r': // Enter key
            if (awaitingArrow == 0)
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
        case 0: // Arrow Keys
            lastArrow = 0; // reset arrows to 0
            // Do nothing
            break;
        default: // add typed character to output buffer
            insertChar(c);
            break;
    };
    quit_conf = 1;
}

// Creates a welcome title to display when there is no file loaded
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

// appends the contents of a file or the lack of file to the output buffer
// This is required every time we refresh the screen
void loadRows(struct outputBuffer* oBuf, int delta){
    scroll(); // updates the cursor position to where it needs to be
    for (int y = 0; y <= screenrows + delta - 1; y++) // load only the size of the screen and ...
        if (y + rowOffset < openedFileLines) { // display file contents within the available space
            int pos = y + rowOffset;
            int len = (fromOpenedFile[pos].size  - colOffset > screencols)
                        ? (screencols)  :  (fromOpenedFile[pos].size - colOffset);
            
            if (len > 0){
                char *line = fromOpenedFile[pos].buf + colOffset;
                unsigned char* lineStatus = &toRenderToScreen[pos].state[colOffset];
                for (int i = 0; i < len; i++)
                    appendWithColor(oBuf, &line[i], 1, lineStatus[i]);
                // after printing the line to the buffer
                appendToBuffer(oBuf, CL_DEFAULT_COLOR); // Reset to the default colors
            }

            appendToBuffer(oBuf, "\r\n", 2);
             
        }
        else { //  no file (left) to load
            if (y < screenrows + delta){
                appendToBuffer(oBuf, "~", 1);
                appendToBuffer(oBuf, "\r\n", 2);
            }
        }
}

// Prepares to render a stutus bar which is appended to the end of the output buffer
// this will be at the bottom two lines of the screen
void loadStatusBar(struct outputBuffer* oBuf){
    appendToBuffer(oBuf, CL_INVERT_COLOR);
    char status[80], rstatus[80];
    const char* modifiedStatus = fileModified ? "*modified" : "";
    int width = snprintf(status, sizeof(status),
                         "%.20s - %d lines %s",
                         filename ? filename : "[Unsaved File]",
                         openedFileLines,
                         modifiedStatus );
    int rwidth = snprintf(rstatus, sizeof(rstatus),
                          "%s | %d/%d",
                          (openedFileFlags) ? openedFileFlags->filetype : "(unknown filetype)",
                          cursorPos.y + rowOffset,
                          openedFileLines);
    if (width > screencols)
        width = screencols;
    appendToBuffer(oBuf, status, width);
    for (; width < screencols; width++){
        if (screencols - width == rwidth) {
            appendToBuffer(oBuf, rstatus, rwidth);
            break;
        }
        else // add space so as to align rstatus to the right
            appendToBuffer(oBuf, " ", 1);
    }
    appendToBuffer(oBuf, CL_FMT_CLEAR);
    appendToBuffer(oBuf, "\r\n", 2);
    
    // Next Line
    // status message
    appendToBuffer(oBuf, CL_LINE_RIGHT_OF_CURSOR); // clear previous status message
    int msgSize = strlen(statusmsg);
    if (msgSize > screencols)
        msgSize = screencols;
    if (msgSize && time(NULL) - statusmsg_time < 7)// display message (for 7 seconds)
        appendToBuffer(oBuf, statusmsg, msgSize);
}

// process a status message and prepares it for output
void loadStatusMessage(const char *fmt, ...){
    va_list additionalArgs;
    va_start(additionalArgs, fmt);
    vsnprintf(statusmsg, sizeof(statusmsg), fmt, additionalArgs);
    va_end(additionalArgs);
    statusmsg_time = time(NULL);
}

// prompts the user for input
// during this process, this will run a function that has been passed as a parameter
char* userPrompt(char* message, void (*func)(char* str, int key)){
    size_t inputSize = 128;
    char* input = malloc(inputSize);
    input[0] = '\0';
    size_t len = 0;
    
    while (1){
        loadStatusMessage(message, input);
        refresh(); // allows the prompt message and the user input to be seen on the screen
                   // through a status message
        
        int charIn = readCharacter();
        // test the user input
        //
        if (charIn == '\x1b'){  // Escape key
            loadStatusMessage("");
            if (func)
                func(input, charIn);
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
            if (awaitingArrow == 0 && len != 0) {
                loadStatusMessage("");
                if (func)
                    func(input, charIn);
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
        
        if (func)
            func(input, charIn);
    }
}

// before refreshing the screen, this function will update the cursopr position and the screen position to the correct position
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
    else if (cursorPos.y > screenrows ){ // cursor is below window,  need to scroll down
        rowOffset = cursorPos.y - screenrows;
        cursorPos.y = screenrows; // return cursorPos to within screen range
    }
    
    // HORIZONTAL SCROLLING
    if (cursorPos.x < colOffset && cursorPos.x == 0) {
        --colOffset;
     }
     if (cursorPos.x > colOffset + screencols) {
         colOffset = cursorPos.x - screencols;
     }
    
    // Consider tabs
    struct outputBuffer* line = &fromOpenedFile[cursorPos.y + rowOffset - 1];
    cursorPos.x = addTabs(line, cursorPos.x);
    
    repositionCursor();
}

// the output buffer and the render to screen buffer are not equal
// tab keys are converted to spaces in the screen buffer
// this function interprets an index without any conversions from the original output buffer
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

// Retreves the current index ( x-position ) in the given output line of the output buffer
// the index takes accoount of the converted tabs into spaces
int addTabs(struct outputBuffer* line, int xPos){
    if (openedFileLines == 0)
        return xPos;
    int index = zeroTabs(line, &xPos);
    return index;
}

// Retreves the current index ( x-position ) in the given output line of the output buffer
// the index ignores any conversion of tabs into spaces
int subtractTabs(struct outputBuffer* line, int xPos){
    if (openedFileLines == 0)
        return xPos;
    zeroTabs(line, &xPos);
    return xPos;
}

// opens a file
void openFile(char* file) {
    free(filename);
    filename = strdup(file);
    FILE* f = fopen(file, "r");
    if (!f)
        failExit("Could not open file");
    else
        detectFileType();
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
    lastArrow = 0;
    awaitingArrow = 0;
}

// Detects the file type of the file which has been openeed
void detectFileType(){
    openedFileFlags = NULL;
    if (filename == NULL)
        return;
    
    char* ext = strrchr(filename, '.');
    
    for (int i = 0; i < databaseSize; i++){
        struct editorFlags* flags = &database[i];
        
        for (int j = 0; flags->recognisedFileList[j]; j++){
            int res = (flags->recognisedFileList[j][0] == '.');
            if ( (res && ext && !strcmp(ext, flags->recognisedFileList[j]) ) || (!res && strstr(filename, flags->recognisedFileList[i] )) ){
                openedFileFlags = flags;
                return;
            }
        }
    }
}

// Adds any live changes by the user to the output buffer
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
    updateStatus(dest);
}

#define isWhiteSpace(c) ( isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL)
// Updates the state of the editor flags for each and every character of the current line passed as a parameter - this adjust the flags to the appropriate mode of output
// Purpose is to allow the outputed text or the backgound to be changed to a custom color
void updateStatus(struct outputBuffer* line){
    line->state = realloc(line->state, line->size);
    memset(line->state,  normal, line->size);
    
    if (openedFileFlags == NULL)
        return;
    
    int i = 0;
    static int isQuote = 0; // boolean, flags a change the color of quoted text
    static int isComment = 0; // 33for changing the color of comments
    int prev_whiteSp = 1; // for recognizing the beginning and end of a non white space char
    
    if (&toRenderToScreen[0] == line){ // Each render of the first line requires that we
        isQuote = 0; // Requires a reset of the
        isComment = 0; // and isComment
    }
    while ( i < line->size){
        
        // Check for comments and shade comments appropriately
        if (openedFileFlags->flags & highlight_comment){
            if (!isQuote && !strncmp( &line->buf[i], "//", 2 )){
                memset(&line->state[i], highlight_comment, line->size);
                i = line->size -1;
                break;
            }
            if (isComment && !isQuote){
                if (!strncmp( &line->buf[i], "*/", 2 )){
                    memset(&line->state[i], highlight_comment, 2);
                    i += 2;
                    isComment = 0;
                    prev_whiteSp = 1;
                    continue;
                }
                else {
                    line->state[i] = highlight_comment;
                    i++;
                    continue;
                }
            }
            if (!isComment && !isQuote && !strncmp( &line->buf[i], "/*", 2)){
                memset(&line->state[i], highlight_comment, 2);
                i += 2;
                isComment = 1;
                continue;
            }
        }
        // Check for string literals and shade them appropriately
        if (openedFileFlags->flags & highlight_string){
            char c = line->buf[i];
            if (isQuote){
                line->state[i] = highlight_string;
                if (c == '\\' && i + 1 < line->size) {
                    line->state[i + 1] = highlight_string;
                    i += 2;
                    continue;
                }
                if (c == isQuote)
                    isQuote = 0;
                i++;
                continue;
            }
            else if (c == '"' || c == '\''){
                isQuote = c;
                line->state[i] = highlight_string;
                i++;
                continue;
            }
            
        }
        // Check for numerical literals and shade them appropriately
        if (openedFileFlags->flags & highlight_num)
            if (isdigit(line->buf[i]))
                line->state[i] = highlight_num;
        
        // Check for keywords and shade them appropriately
        if (prev_whiteSp){
            int idx;
            for (idx = 0; openedFileFlags->recognisedKeywords[idx]; idx++) {
                const char* key = openedFileFlags->recognisedKeywords[idx];
                int length = strlen(key);
                // is this a regular keyword
                int key_regular = (key[length - 1] == '|');
                if (key_regular)
                    length--;
                char c = line->buf[i + length];
                if ( !strncmp(&line->buf[i], key, length)
                     &&  isWhiteSpace(c) )  {
                    memset(&line->state[i], key_regular ? highlight_keyword_regular : highlight_keyword_strong, length);
                    i += length;
                    break;
                }
            }
            if (openedFileFlags->recognisedKeywords[idx] != NULL) {
                    prev_whiteSp = 0;
                    continue;
            }
        }
        prev_whiteSp = isWhiteSpace(line->buf[i]);
        i++;
    }
}

// Appends a string to the end of the output buffer on a new line
// Especially used when opening a file or typing into the editor
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
    fromOpenedFile[at].state = NULL; // leave blank
    memcpy(fromOpenedFile[at].buf, stringLine, readCount);
    fromOpenedFile[at].buf[readCount] = '\0';
    
    // Render tabs properly
    toRenderToScreen = realloc(toRenderToScreen, sizeof(struct outputBuffer) * (openedFileLines + 1));
    toRenderToScreen[at].size = 0;
    toRenderToScreen[at].buf = NULL;
    toRenderToScreen[at].state = NULL;
    updateBuffer(&toRenderToScreen[at], &fromOpenedFile[at]);
    
    openedFileLines += 1;
    fileModified += 1;
}

// Appends a string to the output buffer on a new line
void appendString(struct outputBuffer* source, int line, char* string, size_t len){
    source[line].buf = realloc(source[line].buf, source[line].size + len +1);
    memcpy(&source[line].buf[source[line].size], string, len);
    source[line].size += len;
    source[line].buf[source[line].size] = '\0';
    updateBuffer(&toRenderToScreen[line], &fromOpenedFile[line]);
    fileModified += 1;
}


// Inserts a string to the the output buffer
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

// Inserts a character into the output buffer
void insertChar(int character) {
    int yPos = cursorPos.y + rowOffset - 1;
    int xPos = subtractTabs(&fromOpenedFile[yPos],cursorPos.x + colOffset) -1 ;
    
    if (!fromOpenedFile){
        // Currently on the line after the title
        // because no file is open
        cursorPos.y = 1;
        cursorPos.x = 1;
        xPos = 0;
        yPos = 0;
        insertNewLine(openedFileLines, "", 0);
    }
    else if ( yPos == openedFileLines) {
        insertNewLine(openedFileLines, "", 0);
    }
    insertIntoBuffer(&fromOpenedFile[yPos], xPos, character);
    updateBuffer(&toRenderToScreen[yPos], &fromOpenedFile[yPos]);
    cursorPos.x++;
    for(int i = 0; i < openedFileLines; i++)
        updateStatus(&toRenderToScreen[i]);
}

// Inserts a new line into the output buffer
void insertLine(){
    // when pressing enter
    int yPos = cursorPos.y + rowOffset - 1;
    int xPos = subtractTabs(&fromOpenedFile[yPos],cursorPos.x + colOffset) - 1;
    
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

// Deletes from the output buffer
void deleteFromBuffer(struct outputBuffer* dest, int at){
    if (at < 0 || at > dest->size)
        return; // if not within the bounds of the existing line
    
    memmove(&dest->buf[at], &dest->buf[at +1], dest->size - at);
    dest->size--;
    fileModified += 1;
}

// Deletes a character from the output buffer
void deleteChar(){
    if (!fromOpenedFile){
        return;
    }
    else {
        // Consider tabs
        cursorPos.x = subtractTabs(&fromOpenedFile[cursorPos.y + rowOffset - 1], cursorPos.x + colOffset );
        
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
    for(int i = 0; i < openedFileLines; i++)
        updateStatus(&toRenderToScreen[i]);
}

// Deletes a line from the output buffer
void deleteRow(int at){
    if (at < 0 || at >= openedFileLines)
        return;
    
    free(fromOpenedFile[at].buf);
    free(toRenderToScreen[at].buf);
    free (toRenderToScreen[at].state);
    
    memmove(&fromOpenedFile[at], &fromOpenedFile[at + 1], sizeof(struct outputBuffer) * (openedFileLines - at -1) );
    memmove(&toRenderToScreen[at], &toRenderToScreen[at + 1], sizeof(struct outputBuffer) * (openedFileLines - at -1) );
    
    openedFileLines--;
    fileModified++;
}

// Prepares the whole document into a single string in order to save the file onto the disk
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

// Saves the current document onto the disk
void saveFile(){
    if (filename == NULL){
        filename = userPrompt("Save as : %s", NULL);
        // Status bar uses formated strings
        if (filename == NULL){
            loadStatusMessage("Save aborted.");
            return;
        }
        detectFileType();
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
                for(int i = 0; i < openedFileLines; i++)
                    updateStatus(&toRenderToScreen[i]);
                return;
            }
        }
        close(descriptor);
    }
    free(string);
    loadStatusMessage("Error! Cannot save - I/O error details: %s", strerror(errno));
}

// Searches the document for any occurence of the queried string inputed by the user
void search(){
    awaitingArrow = 1;
    char* query = userPrompt("Search: %s (ESC to cancel | Arrows or Enter to search)", onSearch);
    awaitingArrow = 0;
    if (query == NULL)
        return;
    free(query);
}

// Callback function that is to be called to search the document
// This function also handles inputs from the user for more customized control
void onSearch (char *string, int key){
    static int last = -1;
    static int next = 0;
    static int direction = 1; // positive or forward search
    
    static int saved_line_nr;
    static char* saved_line = NULL;
    
    // Restore state of previously highlighted text
    if (saved_line){
        memcpy(toRenderToScreen[saved_line_nr].state, saved_line, toRenderToScreen[saved_line_nr].size);
        free(saved_line);
        saved_line = NULL;
    }
    
    if ( key == '\x1b') {// Entered key == Esc
        last = -1; // reset
        next = 0;
        direction =1;
        return; // and exit search
    }
    else if ( lastArrow==2 || lastArrow ==4 || key == '\r'){ // down, right and enter
        direction = 1;
        lastArrow = 0;
    }
    else if ( lastArrow == 1 || lastArrow == 3 ){ //up and left
        direction = -1;
        lastArrow = 0;
        next = 0;
    }
    else { // query string is changed, therefor, the search is to be started again
        last = -1;
        next = 0;
        direction = 1;
    }
    
    if (last == -1)
        direction = 1;
    int current = last;
    
    for (int i = 0; i < openedFileLines; i++){
        if (next != 0  )
            next += direction;
        else
            current += direction;
        
        if (current == -1)
            current = openedFileLines -1;
        else if (current == openedFileLines)
            current = 0;
        
        
        struct outputBuffer* line = &toRenderToScreen[current];
        char* match = strstr(line->buf + next, string);
    
        if (match){
            last = current;
            next = match - line->buf;
            
            cursorPos.y = current+1;
            cursorPos.x = match - line->buf +1;
            
            
            rowOffset = 0;
            // Save state of buffer for later restoration
            saved_line_nr = current;
            saved_line = malloc(line->size);
            memcpy(saved_line, line->state, line->size);

            // change state to Highilight the found text
            memset(&line->state[match - line->buf ], highlight_match, strlen(string));
            break;
        }
        else
            next = 0;
    }
}

