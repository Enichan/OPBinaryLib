/*
//  MIT License
//
//  Copyright (c) 2023 Eniko Fox/Emma Maassen
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
#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE
#define strdup _strdup
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "..\opblib.h"

#define OPL_IMPLEMENTATION
#include "opl.h"

#define SAMPLE_RATE 44100

// OPL methods, these will depend on your OPL emulator. Here we use dos-like's opl.h implementation:
// https://github.com/mattiasgustavsson/dos-like/blob/bcdec4259db66c764dbf02f16e2bc40198924091/source/libs/opl.h
static opl_t* OPL_Init(void) {
    return opl_create();
}

static void OPL_Render(void* chip, short* buffer, int samplePairs, float volume) {
    if (samplePairs <= 0) {
        return;
    }
    opl_render((opl_t*)chip, buffer, samplePairs, volume);
}

static void OPL_Write(void* chip, int count, uint16_t* regs, uint8_t* data) {
    opl_write((opl_t*)chip, count, regs, data);
}

// sets registers so all channels should stop producing sound
static void OPL_Clear(void* chip) {
    uint16_t regs[0x54];
    uint8_t data[0x54];

    for (int j = 0, k = 0; j < 0x200; j += 0x100) {
        for (int i = 0; i < 0x16; i++, k++) {
            regs[k] = 0x40 + i + j;
            data[k] = 0xFF;
        }
    }

    OPL_Write(chip, 0x16 * 2, regs, data);

    for (int j = 0, k = 0; j < 0x200; j += 0x100) {
        for (int i = 0; i < 9; i++, k++) {
            regs[k] = 0xB0 + i + j;
            data[k] = 0;
        }
    }

    OPL_Write(chip, 18, regs, data);
}

// CommandStream is a simple dynamic array which doubles in size when it runs out of capacity
typedef struct CommandStream {
    size_t Count;
    size_t Capacity;
    OPB_Command* Stream;
} CommandStream;

static inline int CommandStream_AdjustCapacity(CommandStream* cmds, size_t count) {
    if (count >= cmds->Capacity) {
        size_t newCapacity = cmds->Capacity < 16 ? 16 : cmds->Capacity;
        while (count >= newCapacity) {
            newCapacity *= 2; // double until capacity is greater-equal to number of items
        }

        OPB_Command* newStream = calloc(newCapacity, sizeof(OPB_Command));
        if (newStream == NULL) {
            printf("Out of memory in ReceiveOpbBuffer\n");
            return -1; // error
        }

        if (cmds->Stream != NULL) {
            // copy previous item over to new buffer
            memcpy(newStream, cmds->Stream, sizeof(OPB_Command) * cmds->Count);
            // release old buffer
            free(cmds->Stream); 
        }

        // set new buffer and capacity
        cmds->Stream = newStream;
        cmds->Capacity = newCapacity;
    }
    return 0; // success
}

// used to get the exe's name when printing usage directions
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

// shoves OPB_Commands from OPB_FileToOpl into our dynamic array defined by CommandStream
int ReceiveOpbBuffer(OPB_Command* commandStream, size_t commandCount, void* context) {
    CommandStream* cmds = (CommandStream*)context;

    // increase capacity if necessary
    if (CommandStream_AdjustCapacity(cmds, cmds->Count + commandCount)) {
        return -1; // out of memory
    }

    // add items
    for (size_t i = 0; i < commandCount; i++) {
        cmds->Stream[cmds->Count++] = commandStream[i];
    }

    return 0;
}

// some methods that make writing to file cleaner
static void WriteError() {
    printf("File write error");
    exit(EXIT_FAILURE);
}

static inline void WriteChars(FILE* file, const char* value, int count) {
    if (fwrite(value, sizeof(char), count, file) != count) WriteError();
}

static inline void WriteShorts(FILE* file, const short* value, int count) {
    if (fwrite(value, sizeof(short), count, file) != count) WriteError();
}

static inline void WriteUInt32(FILE* file, const uint32_t value) {
    if (fwrite(&value, sizeof(uint32_t), 1, file) != 1) WriteError();
}

static inline void WriteUInt16(FILE* file, const uint16_t value) {
    if (fwrite(&value, sizeof(uint16_t), 1, file) != 1) WriteError();
}

// this is a buffer that holds the audio samples generated by the OPL emulator
// our OPL sound sample buffer should hold 1 second (so equal to sample rate) of audio
// but OPL3 is stereo so we need twice as many actual samples as sample pairs
#define MAX_SAMPLES 44100
short buffer[MAX_SAMPLES * 2];

// logger
static void Logger(const char* s) {
    printf(s);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        char* path = argv[0];
        char filename[128];
        GetFilename(path, filename, 128);

        printf("Usage: %s <source.opb> <dest.wav>\n", filename);
        exit(EXIT_FAILURE);
    }

    // set logger
    OPB_Log = Logger;

    CommandStream commands = { 0 };

    // unpack OPB file into OPL3 command stream
    printf("Unpacking %s\n", argv[1]);

    int error;
    if ((error = OPB_FileToOpl(argv[1], ReceiveOpbBuffer, &commands)) != 0) {
        printf("Error converting OPB file: %s\n", OPB_GetErrorMessage(error));
        exit(EXIT_FAILURE);
    }

    // open wav file and write header (write end offset and data length after)
    printf("Writing %s\n", argv[2]);
    FILE* fout = fopen(argv[2], "wb");
    WriteChars(fout, "RIFF", 4);
    WriteUInt32(fout, 0); // file end offset (data length + 36)
    WriteChars(fout, "WAVE", 4);
    WriteChars(fout, "fmt ", 4);
    WriteUInt32(fout, 16);
    WriteUInt16(fout, 1); // WAVE_FORMAT_PCM
    WriteUInt16(fout, 2); // channel 1=mono, 2=stero
    WriteUInt32(fout, SAMPLE_RATE);
    WriteUInt32(fout, SAMPLE_RATE * 2 * (16 / 8)); // bytes/sec
    WriteUInt16(fout, 2 * (16 / 8)); // block size
    WriteUInt16(fout, 16); // bits per sample
    WriteChars(fout, "data", 4);
    WriteUInt32(fout, 0); // data length

    // initialize OPL emulator and start processing commands/generating audio!
    printf("Initializing OPL emulator\n");
    opl_t* opl = OPL_Init();
    double time = 0;

    printf("Processing OPL command stream and writing audio samples\n");
    for (size_t i = 0; i < commands.Count; i++) {
        OPB_Command cmd = commands.Stream[i];

        if (cmd.Time > time) {
            // time has advanced, generate audio samples before sending this command to the OPL emulator
            double elapsed = cmd.Time - time;
            time = cmd.Time;

            // number of sample pairs to generate depends on sample rate
            int samples = (int)(elapsed * SAMPLE_RATE);
            while (samples > 0) {
                int count = samples <= MAX_SAMPLES ? samples : MAX_SAMPLES;
                OPL_Render(opl, buffer, count, 0.95f); // 0.95 to prevent clipping
                samples -= count;

                // write out all our sample pairs, this is count * 2 because of the number of channels
                WriteShorts(fout, buffer, count * 2);
            }
        }

        // send command to OPL emulator
        OPL_Write(opl, 1, &cmd.Addr, &cmd.Data);
    }

    size_t filelen = ftell(fout);

    // set wav header file end offset (which is file length - 8)
    fseek(fout, 4, SEEK_SET);
    WriteUInt32(fout, (uint32_t)(filelen - 8));

    // set wav header data size (which is file length - 44, which is the size of the header)
    fseek(fout, 40, SEEK_SET);
    WriteUInt32(fout, (uint32_t)(filelen - 44));

    // done!
    fclose(fout);
    printf("Done!\n");

    // clean up
    free(opl);
    opl = NULL;
}