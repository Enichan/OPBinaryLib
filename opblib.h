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

    #define OPBERR_LOGGED 1 // an error occurred and what error that was has been sent to OPB_Log
    #define OPBERR_WRITE_ERROR 2
    #define OPBERR_SEEK_ERROR 3
    #define OPBERR_TELL_ERROR 4
    #define OPBERR_READ_ERROR 5
    #define OPBERR_BUFFER_ERROR 6
    #define OPBERR_NOT_AN_OPB_FILE 7
    #define OPBERR_VERSION_UNSUPPORTED 8

    typedef struct OPB_Command {
        uint16_t Addr;
        uint8_t Data;
        double Time;
    } OPB_Command;

    typedef enum OPB_Format {
        OPB_Format_Default,
        OPB_Format_Raw,
    } OPB_Format;

    const char* OPB_GetErrorMessage(int errCode);

    const char* OPB_GetFormatName(OPB_Format fmt);

    // Custom write handler of the same form as stdio.h's fwrite for writing to memory
    // This function should write elementSize * elementCount bytes from buffer to the user-defined context object
    // Must return elementCount if successful
    typedef size_t(*OPB_StreamWriter)(const void* buffer, size_t elementSize, size_t elementCount, void* context);

    // Custom seek handler of the same form as stdio.h's fseek for writing to memory
    // This function should change the position to write to in the user-defined context object by the number of bytes
    // Specified by offset, relative to the specified origin which is one of 3 values:
    //
    // 1. Beginning of file (same as fseek's SEEK_SET)
    // 2. Current position of the file pointer (same as fseek's SEEK_CUR)
    // 3. End of file (same as fseek's SEEK_END)
    //
    // Must return 0 if successful
    typedef int (*OPB_StreamSeeker)(void* context, long offset, int origin);
    
    // Custom tell handler of the same form as stdio.h's ftell for writing to memory
    // This function must return the current write position for the user-defined context object
    // Must return -1L if unsuccessful
    typedef long (*OPB_StreamTeller)(void* context);

    // Custom read handler of the same form as stdio.h's fread for reading from memory
    // This function should read elementSize * elementCount bytes from the user-defined context object to buffer
    // Should return number of elements read
    typedef size_t(*OPB_StreamReader)(void* buffer, size_t elementSize, size_t elementCount, void* context);

    // Function that receives OPB_Command items read by OPB_BinaryToOpl and OPB_FileToOpl
    // This is where you copy the OPB_Command items into a data structure or the user-defined context object
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
