#include "editor.h"

int main (int argc, char* argv[]) {
    // First turn of Echo mode and canonical mode
    turnOfFlags();
    editorSize(); // get the editor size
    if (argc > 1)
        openFile(argv[1]);
    refresh();
    while (1) {
        processKey();
    }
    return 0;
}
