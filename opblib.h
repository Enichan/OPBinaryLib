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
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct OPB_File OPB_File;

    // uncomment this for big endian architecture
    //#define OPB_BIG_ENDIAN

    // uncomment to only compile the OPB reading parts of library
    //#define OPB_READONLY

    // uncomment to disable dependence on C standard library (sets OPB_READONLY)
    // OPB_NOSTDLIB requires providing an instrument buffer, see OPB_ProvideInstrumentBuffer
    #define OPB_NOSTDLIB

    #define OPBERR_LOGGED 1 // an error occurred and what error that was has been sent to OPB_Log
    #define OPBERR_WRITE_ERROR 2
    #define OPBERR_SEEK_ERROR 3
    #define OPBERR_TELL_ERROR 4
    #define OPBERR_READ_ERROR 5
    #define OPBERR_BUFFER_ERROR 6
    #define OPBERR_NOT_AN_OPB_FILE 7
    #define OPBERR_VERSION_UNSUPPORTED 8
    #define OPBERR_OUT_OF_MEMORY 9
    #define OPBERR_DISPOSED 10
    #define OPBERR_INVALID_BUFFER 11
    #define OPBERR_NO_INSTRUMENT_BUFFER 12
    #define OPBERR_INSTRUMENT_BUFFER_SIZE_OVERFLOW 13
    #define OPBERR_VECTOR_ERROR 14
    #define OPBERR_VEC_INDEX_OUT_OF_RANGE 15
    #define OPBERR_NULL_INSTANCE 16
    #define OPBERR_INSTRUMENT_BUFFER_ERROR 17
    #define OPBERR_INSTRUMENT_BUFFER_SIZE 18

    typedef enum OPB_Format {
        OPB_Format_Default,
        OPB_Format_Raw,
    } OPB_Format;

    typedef struct OPB_Command {
        uint16_t Addr;
        uint8_t Data;
        double Time;
    } OPB_Command;

    typedef struct OPB_HeaderInfo {
        OPB_Format Format;
        size_t SizeBytes;
        size_t InstrumentCount;
        size_t ChunkCount;
    } OPB_HeaderInfo;

    typedef struct OPB_Instrument OPB_Instrument;

    const char* OPB_GetErrorMessage(int errCode);

    const char* OPB_GetFormatName(OPB_Format fmt);

    // Custom write handler of the same form as stdio.h's fwrite for writing to memory
    // This function should write elementSize * elementCount bytes from buffer to the user-defined context object
    // Must return elementCount if successful
    typedef size_t(*OPB_StreamWriter)(const void* buffer, size_t elementSize, size_t elementCount, void* context);

    // Custom seek handler of the same form as stdio.h's fseek for reading from/writing to memory
    // This function should change the position to write to in the user-defined context object by the number of bytes
    // Specified by offset, relative to the specified origin which is one of 3 values:
    //
    // 1. Beginning of file (same as fseek's SEEK_SET)
    // 2. Current position of the file pointer (same as fseek's SEEK_CUR)
    // 3. End of file (same as fseek's SEEK_END)
    //
    // Must return 0 if successful
    typedef int (*OPB_StreamSeeker)(void* context, long offset, int origin);
    
    // Custom tell handler of the same form as stdio.h's ftell for reading from/writing to memory
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


    // Open OPB binary for reading through OPB_File instance. Returns 0 if successful.
    int OPB_OpenStream(OPB_StreamReader read, OPB_StreamSeeker seek, OPB_StreamTeller tell, void* userData, OPB_File* opb);

#ifndef OPB_NOSTDLIB
    // Open OPB file for reading through OPB_File instance. Returns 0 if successful.
    int OPB_OpenFile(const char* filename, OPB_File* opb);
#endif

    // Get OPB_File header information
    OPB_HeaderInfo OPB_GetHeaderInfo(OPB_File* opb);

    // Read a maximum of `count` OPL commands into array `buffer`. Returns the number of items read,
    // or zero if the end of the OPL command stream was reached or an error occurred. In the case of
    // an error, errorCode will be set to a non-zero value.
    int OPB_ReadBuffer(OPB_File* opb, OPB_Command* buffer, int count, int* errorCode);

#ifndef OPB_NOSTDLIB
    // Reads to the end of the OPL command stream and returns an array containing the read commands
    // containing `count` items. If an error occurs the returned value will be NULL and errorCode
    // will be set to a non-zero value.
    OPB_Command* OPB_ReadToEnd(OPB_File* opb, size_t* count, int* errorCode);
#endif

    // Rewind OPB_File instance back to the start of the OPL command stream. Returns 0 if successful.
    int OPB_Reset(OPB_File* opb);

    // Dispose of OPB_File instance
    void OPB_Free(OPB_File* opb);

    // Provide buffer for instruments for OPB_File instance to use instead of allowing it to use calloc
    // to allocate the buffer itself. This call must be made after OPB_OpenStream/OPB_OpenFile and before
    // any other functions are called, or it will return an error. The caller is responsible for freeing
    // the memory associated with the instrument buffer. Returns 0 if successful.
    // 
    // OPB_GetHeaderInfo can be used to get the instrument count for the current OPB_File instance.
    //
    // NOTE: This call *must* be used when compiling with OPB_NOSTDLIB or attempting to read from the
    // OPB_File instance *will* fail!
    int OPB_ProvideInstrumentBuffer(OPB_File* opb, OPB_Instrument* buffer, size_t capacity);


    // OPBLib log function
    typedef void (*OPB_LogHandler)(const char* s);
    extern OPB_LogHandler OPB_Log;

    // necessary typedefs
    typedef struct OPB_Operator {
        int16_t Characteristic;
        int16_t AttackDecay;
        int16_t SustainRelease;
        int16_t WaveSelect;
    } OPB_Operator;

    typedef struct OPB_Instrument {
        int16_t FeedConn;
        OPB_Operator Modulator;
        OPB_Operator Carrier;
        size_t Index;
    } OPB_Instrument;

    typedef struct OPB_Chunk {
        size_t LoCount;
        size_t Count;
        size_t Index;
    } OPB_Chunk;

    typedef struct OPB_ReadContext {
        int UserDataDisposeType;
        void* UserData;
        OPB_StreamReader Read;
        OPB_StreamSeeker Seek;
        OPB_StreamTeller Tell;
        bool FreeInstruments;
        OPB_Instrument* Instruments;
        bool InstrumentsInitialized;
        size_t InstrumentCapacity;
        double Time;

        size_t ChunkIndex;
        OPB_Chunk CurrentChunk;

        OPB_Command CommandBuffer[16];
        int BufferCount;
        int BufferIndex;

        OPB_Format Format;
        size_t SizeBytes;
        size_t InstrumentCount;
        size_t ChunkCount;
        size_t ChunkDataOffset;
    } OPB_ReadContext;

    typedef struct OPB_File {
        OPB_ReadContext context;
        bool disposedValue;
    } OPB_File;

    // DEPRECATED: Use OPB_File via OPB_OpenStream
    // OPB binary to OPL command stream. Returns 0 if successful.
    int OPB_BinaryToOpl(OPB_StreamReader reader, void* readerData, OPB_BufferReceiver receiver, void* receiverData);

#ifndef OPB_NOSTDLIB
    // DEPRECATED: Use OPB_File via OPB_OpenFile
    // OPB file to OPL command stream. Returns 0 if successful.
    int OPB_FileToOpl(const char* file, OPB_BufferReceiver receiver, void* receiverData);
#endif

#ifdef __cplusplus
}
#endif
