#include "editor.h"

// if you run 'bin/main.o absolutepath/xxx.txt', It should open text file.
int main (int argc, char* argv[]) {
    // First turn of Echo mode and canonical mode
    turnOfFlags();
    editorInit();
    if (argc > 1)
        openFile(argv[1]);
    refresh();
    while (1) {
        processKey();
        refresh();
    }
    return 0;
}
