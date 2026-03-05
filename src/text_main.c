#include <punix.h>
#include <string.h>

void text_editor(const char* edit_filename);

int main(int argc, char** argv) {
    if (argc > 1) {
        text_editor(argv[1]);
    } else {
        text_editor("");
    }
    return 0;
}
