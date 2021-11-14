/*
//  MIT License
//
//  Copyright (c) 2021 Eniko Fox/Emma Maassen
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
*/
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    // uncomment this for big endian architecture
    //#define OPB_BIG_ENDIAN

    #define OPBERR_LOGGED 1
    #define OPBERR_WRITE_ERROR 2
    #define OPBERR_SEEK_ERROR 3
    #define OPBERR_TELL_ERROR 4
    #define OPBERR_READ_ERROR 5
    #define OPBERR_BUFFER_ERROR 6

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
    typedef size_t(*OPB_StreamWriter)(const void* buffer, size_t elementSize, size_t elementCount, void* context);

    // must return 0 if successful
    typedef int (*OPB_StreamSeeker)(void* context, long offset, int origin);
    
    // must return -1L is unsuccessful
    typedef long (*OPB_StreamTeller)(void* context);

    // should return number of elements read
    typedef size_t(*OPB_StreamReader)(void* buffer, size_t elementSize, size_t elementCount, void* context);

    // Should return 0 if successful. Note that the array for `commandStream` is stack allocated and must be copied!
    typedef int(*OPB_BufferReceiver)(OPB_Command* commandStream, size_t commandCount, void* context);

    // OPL command stream to binary. Returns 0 if successful.
    int OPB_OplToBinary(OPB_Format format, OPB_Command* commandStream, size_t commandCount,
        OPB_StreamWriter write, OPB_StreamSeeker seek, OPB_StreamTeller tell, void* userData);

    // OPL command stream to file. Returns 0 if successful.
    int OPB_OplToFile(OPB_Format format, OPB_Command* commandStream, size_t commandCount, const char* file);

    // OPB binary to OPL command stream. Returns 0 if successful.
    int OPB_BinaryToOpl(OPB_StreamReader reader, void* readerData, OPB_BufferReceiver receiver, void* receiverData);

    // OPB file to OPL command stream. Returns 0 if successful.
    int OPB_FileToOpl(const char* file, OPB_BufferReceiver receiver, void* receiverData);

    // OPBLib log function
    typedef void (*OPB_LogHandler)(const char* s);
    extern OPB_LogHandler OPB_Log;

#ifdef __cplusplus
}
#endif
