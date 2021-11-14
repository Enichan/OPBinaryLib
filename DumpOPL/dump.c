#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "..\opblib.h"

void GetFilename(char* path, char* result, size_t maxLen) {
    int lastSlash = -1;
    int i = 0;
    while (path[i] != '\0') {
        if (path[i] == '/' || path[i] == '\\') lastSlash = i;
        i++;
    }
    int pathLen = i;

    if (lastSlash >= 0) strncpy(result, path + lastSlash + 1, (size_t)(maxLen - 1));
    else strncpy(result, path, (size_t)(maxLen - 1));
    result[maxLen - 1] = '\0';
}

int ReceiveOpbBuffer(OPB_Command* commandStream, size_t commandCount, void* context) {
    FILE* file = (FILE*)context;
    for (int i = 0; i < commandCount; i++) {
        printf("%1.3f: 0x%03X, 0x%02X\n", commandStream[i].Time, commandStream[i].Addr, commandStream[i].Data);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        char* path = argv[0];
        char filename[128];
        GetFilename(path, filename, 128);

        printf("Usage: %s <file>\n", filename);
        printf("Format is time: register, data\n");
        exit(EXIT_FAILURE);
    }

    if (OPB_FileToOpl(argv[1], ReceiveOpbBuffer, NULL)) {
        printf("Error trying to dump OPL\n");
        exit(EXIT_FAILURE);
    }
}