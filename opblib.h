#pragma once
#include <stdint.h>
#include <iostream>
#include <string>
#include <sstream>

#ifdef __cplusplus
extern "C" {
#endif

    #define OPBERR_LOGGED 1
    #define OPBERR_WRITE_ERROR 2
    #define OPBERR_SEEK_ERROR 3
    #define OPBERR_TELL_ERROR 4

    typedef struct OPB_Command {
        uint16_t Addr;
        uint8_t Data;
        double Time;
    } OPB_Command;

    typedef enum OPB_Format {
        OPB_Format_Default,
        OPB_Format_Raw,
    } OPB_Format;

    // must return elementCount if successful
    typedef size_t(*StreamWriter)(const void* buffer, size_t elementSize, size_t elementCount, void* context);

    // must return 0 if successful
    typedef int (*StreamSeeker)(void* context, long offset, int origin);
    
    // must return -1L is unsuccessful
    typedef long (*StreamTeller)(void* context);

    // OPL command stream to binary. Returns 0 if successful.
    int OPB_OplToBinary(OPB_Format format, OPB_Command* commandStream, size_t commandCount,
        StreamWriter write, StreamSeeker seek, StreamTeller tell, void* userData);

    // OPL command stream to file. Returns 0 if successful.
    int OPB_OplToFile(OPB_Format format, OPB_Command* commandStream, size_t commandCount, const char* file);

    // OPB binary to OPL command stream. Returns 0 if successful.

    // OPB file to OPL command stream. Returns 0 if successful.

    // OPBLib log function
    typedef void (*OPB_LogHandler)(const char* s);
    extern OPB_LogHandler OPB_Log;

#ifdef __cplusplus
}
#endif
