#include "editor.h"

int main () {
    // First turn of Echo mode and canonical mode
    turnOfFlags();
    editorSize(); // get the editor size
    refresh();
    while (1) {
        processKey();
    }
    return 0;
}
