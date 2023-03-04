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
#ifdef _WIN32
#define _CRT_SECURE_NO_DEPRECATE
#endif

#include "opblib.h"

#ifdef OPB_NOSTDLIB
#define OPB_READONLY
#endif

#ifndef OPB_NOSTDLIB
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#else
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif
#endif

#define USERDATA_DISPOSE_NONE 0
#define USERDATA_DISPOSE_FREE 1
#define USERDATA_DISPOSE_FCLOSE 2

#define OPB_STRINGIZE_DETAIL(x) #x
#define OPB_STRINGIZE(x) OPB_STRINGIZE_DETAIL(x)
#define OPB_SOURCE_FILENAME "opblib.c"

#define OPB_HEADER_SIZE 7
#define OPB_DATA_START (OPB_HEADER_SIZE + 13)
#define OPB_INSTRUMENT_SIZE 9

// OPBin1\0
const char OPB_Header[OPB_HEADER_SIZE] = { 'O', 'P', 'B', 'i', 'n', '1', '\0' };

#ifndef OPB_NOSTDLIB
#define VECTOR_MIN_CAPACITY 8
#define VECTOR_PTR(vector, index) (void*)((uint8_t*)((vector)->Storage) + (index) * vector->ElementSize)
// this only exists to make type declarations clearer
#define VectorT(T) Vector

typedef struct Vector {
    size_t Count;
    size_t Capacity;
    size_t ElementSize;
    void* Storage;
} Vector;

Vector Vector_New(size_t elementSize) {
    Vector v = { 0 };
    v.ElementSize = elementSize;
    return v;
}

static void Vector_Free(Vector* v) {
    if (v->Storage != NULL) {
        free(v->Storage);
    }
    v->Storage = NULL;
    v->Capacity = 0;
    v->Count = 0;
}

static void* Vector_Get(Vector* v, int index) {
    if (index < 0 || index >= v->Count) {
        return NULL;
    }
    if (v->ElementSize <= 0) {
        return NULL;
    }
    return VECTOR_PTR(v, index);
}
#define Vector_GetT(T, vector, index) ((T*)Vector_Get(vector, index))

static int Vector_Set(Vector* v, void* item, int index) {
    if (index < 0 || index >= v->Count) {
        return -1;
    }
    if (v->ElementSize <= 0) {
        return -1;
    }
    memcpy(VECTOR_PTR(v, index), item, v->ElementSize);
    return 0;
}

static int Vector_Add(Vector* v, void* item) {
    if (v->ElementSize <= 0) {
        return -1;
    }
    if (v->Count >= v->Capacity) {
        size_t newCapacity = v->Capacity * 2;
        if (newCapacity < VECTOR_MIN_CAPACITY) newCapacity = VECTOR_MIN_CAPACITY;

        void* newStorage = malloc(newCapacity * v->ElementSize);
        if (newStorage == NULL) {
            return -1;
        }

        if (v->Storage != NULL) {
            memcpy(newStorage, v->Storage, v->Count * v->ElementSize);
            free(v->Storage);
        }

        v->Storage = newStorage;
        v->Capacity = newCapacity;
    }

    v->Count++;
    return Vector_Set(v, item, (size_t)((int)v->Count - 1));
}

static int Vector_AddRange(Vector* v, void* items, size_t count) {
    if (v->ElementSize <= 0) {
        return -1;
    }
    uint8_t* itemBytes = (uint8_t*)items;
    for (size_t i = 0; i < count; i++, itemBytes += v->ElementSize) {
        int ret;
        ret = Vector_Add(v, (void*)itemBytes);
        if (ret) return ret;
    }
    return 0;
}

typedef int(*VectorSortFunc)(const void* a, const void* b);

static void Vector_Clear(Vector* v, bool keepStorage) {
    v->Count = 0;
    if (!keepStorage && v->Storage != NULL) {
        free(v->Storage);
        v->Storage = NULL;
        v->Capacity = 0;
    }
}

static void Vector_Sort(Vector* v, VectorSortFunc sortFunc) {
    qsort(v->Storage, v->Count, v->ElementSize, sortFunc);
}

#define LOG_VECTOR_OUT_OF_RANGE_ERR(value) Log("Vector index out of range error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n")
#define LOG_VECTOR_GENERIC_ERR(value) Log("Vector error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n")

#define RET_LOG_VECTOR_OUT_OF_RANGE_ERR(value) {\
    LOG_VECTOR_OUT_OF_RANGE_ERR(value); \
    return value; \
}
#define RET_LOG_VECTOR_GENERIC_ERR(value) {\
    LOG_VECTOR_GENERIC_ERR(value); \
    return value; \
}

#endif

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)

#define NUM_CHANNELS 18
#define NUM_TRACKS (NUM_CHANNELS + 1)

#define SEEK(context, offset, origin) \
    if (context->Seek(context->UserData, offset, origin)) { \
        Log("OPB seek error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_SEEK_ERROR; \
    }
#define TELL(context, var) \
    var = context->Tell(context->UserData); \
    if (var == -1L) { \
        Log("OPB file position error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_TELL_ERROR; \
    }

#define READ(buffer, size, count, context) \
    if (context->Read(buffer, size, count, context->UserData) != count) { \
        Log("OPB read error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_READ_ERROR; \
    }
#define READ_UINT7(var, context) \
    if ((var = ReadUint7(context)) < 0) { \
        Log("OPB read error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_READ_ERROR; \
    }

#define SUBMIT(stream, count, context) \
    if (context->Submit(stream, count, context->ReceiverData)) return OPBERR_BUFFER_ERROR

typedef struct Command Command;
typedef struct OpbData OpbData;

#ifndef OPB_NOSTDLIB
static size_t ReadFromFile(void* buffer, size_t elementSize, size_t elementCount, void* context) {
    return fread(buffer, elementSize, elementCount, (FILE*)context);
}

static int SeekInFile(void* context, long offset, int origin) {
    return fseek((FILE*)context, offset, origin);
}

static long TellInFile(void* context) {
    return ftell((FILE*)context);
}
#endif

#ifndef OPB_READONLY
static size_t WriteToFile(const void* buffer, size_t elementSize, size_t elementCount, void* context) {
    return fwrite(buffer, elementSize, elementCount, (FILE*)context);
}

#define WRITE(buffer, size, count, context) \
    if (context->Write(buffer, size, count, context->UserData) != count) { \
        Log("OPB write error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_WRITE_ERROR; \
    }

#define WRITE_UINT7(context, value) \
    if (WriteUint7(context, value)) { \
        Log("OPB write error occurred in '"OPB_SOURCE_FILENAME"' at line "OPB_STRINGIZE(__LINE__)"\n"); \
        return OPBERR_WRITE_ERROR; \
    }

typedef struct WriteContext {
    VectorT(Command) CommandStream;
    OPB_StreamWriter Write;
    OPB_StreamSeeker Seek;
    OPB_StreamTeller Tell;
    OPB_Format Format;
    VectorT(OpbData) DataMap;
    VectorT(OPB_Instrument) Instruments;
    VectorT(Command) Tracks[NUM_TRACKS];
    void* UserData;
} WriteContext;

static void WriteContext_Free(WriteContext* context) {
    Vector_Free(&context->CommandStream);
    Vector_Free(&context->Instruments);
    Vector_Free(&context->DataMap);
    for (int i = 0; i < NUM_TRACKS; i++) {
        Vector_Free(&context->Tracks[i]);
    }
}
#endif

static void ReadContext_Free(OPB_ReadContext* context) {
    if (context->Instruments != NULL) {
#ifndef OPB_NOSTDLIB
        if (context->FreeInstruments) {
            free(context->Instruments);
        }
#endif
        context->Instruments = NULL;
    }
    if (context->UserData && context->UserDataDisposeType) {
#ifndef OPB_NOSTDLIB
        if (context->UserDataDisposeType == USERDATA_DISPOSE_FREE) {
            free(context->UserData);
        }
        else if (context->UserDataDisposeType == USERDATA_DISPOSE_FCLOSE) {
            fclose(context->UserData);
        }
#endif
        context->UserData = NULL;
    }
}

OPB_LogHandler OPB_Log;

#ifndef OPB_NOSTDLIB
static inline size_t BufferSize(const char* format, ...) {
    va_list args;
    va_start(args, format);
    size_t result = vsnprintf(NULL, 0, format, args);
    va_end(args);
    return (size_t)(result + 1); // safe byte for \0
}

static void Log(const char* format, ...) {
    if (!OPB_Log) return;

    va_list args;

    va_start(args, format);
    size_t size = BufferSize(format, args);
    va_end(args);

    if (size == 0) return;

    va_start(args, format);
    char* s = NULL;
    if (size < 0 || (s = (char*)malloc(size)) == NULL) {
        vprintf(format, args);
    }
    else {
        vsprintf(s, format, args);
    }
    va_end(args);

    if (s != NULL) {
        OPB_Log(s);
        free(s);
    }
}
#else
static void Log(const char* s) {
    if (!OPB_Log) return;
    if (s != NULL) {
        OPB_Log(s);
    }
}
#endif

const char* OPB_GetFormatName(OPB_Format fmt) {
    switch (fmt) {
    default:
        return "Default";
    case OPB_Format_Raw:
        return "Raw";
    }
}

static inline uint32_t FlipEndian32(uint32_t val) {
#ifdef OPB_BIG_ENDIAN
    return val;
#else
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
#endif
}

static inline uint16_t FlipEndian16(uint16_t val) {
#ifdef OPB_BIG_ENDIAN
    return val;
#else
    return (val << 8) | ((val >> 8) & 0xFF);
#endif
}

static int Uint7Size(uint32_t value) {
    if (value >= 2097152) {
        return 4;
    }
    else if (value >= 16384) {
        return 3;
    }
    else if (value >= 128) {
        return 2;
    }
    else {
        return 1;
    }
}

typedef struct Command {
    uint16_t Addr;
    uint8_t Data;
    double Time;
    int OrderIndex;
    int DataIndex;
} Command;

#ifndef OPB_READONLY
typedef struct OpbData {
    uint32_t Count;
    uint8_t Args[16];
} OpbData;

static void OpbData_WriteUint7(OpbData* data, uint32_t value) {
    if (value >= 2097152) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = ((value & 0b0111111100000000000000) >> 14) | 0b10000000;
        uint8_t b3 = (value & 0b11111111000000000000000000000) >> 21;
        data->Args[data->Count] = b0; data->Count++;
        data->Args[data->Count] = b1; data->Count++;
        data->Args[data->Count] = b2; data->Count++;
        data->Args[data->Count] = b3; data->Count++;
    }
    else if (value >= 16384) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = (value & 0b0111111100000000000000) >> 14;
        data->Args[data->Count] = b0; data->Count++;
        data->Args[data->Count] = b1; data->Count++;
        data->Args[data->Count] = b2; data->Count++;
    }
    else if (value >= 128) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = (value & 0b011111110000000) >> 7;
        data->Args[data->Count] = b0; data->Count++;
        data->Args[data->Count] = b1; data->Count++;
    }
    else {
        uint8_t b0 = value & 0b01111111;
        data->Args[data->Count] = b0; data->Count++;
    }
}

static void OpbData_WriteU8(OpbData* data, uint32_t value) {
    data->Args[data->Count] = (uint8_t)value;
    data->Count++;
}
#endif

#define OPB_CMD_SETINSTRUMENT 0xD0
#define OPB_CMD_PLAYINSTRUMENT 0xD1
#define OPB_CMD_NOTEON 0xD7

static inline bool IsSpecialCommand(int addr) {
    addr = addr & 0xFF;
    return addr >= 0xD0 && addr <= 0xDF;
}

#define NUM_OPERATORS 36
static int OperatorOffsets[NUM_OPERATORS] = {
    0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x110, 0x111, 0x112, 0x113, 0x114, 0x115,
};

static int ChannelToOp[NUM_CHANNELS] = {
    0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32,
};

static int ChannelToOffset[NUM_CHANNELS] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,
    0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108,
};

static int RegisterOffsetToChannel(uint32_t offset) {
    uint32_t baseoff = offset & 0xFF;
    int chunk = baseoff / 8;
    int suboff = baseoff % 8;

    if (chunk >= 3 || suboff >= 6) {
        return -1;
    }
    return chunk * 3 + (suboff % 3) + ((offset & 0x100) != 0 ? NUM_CHANNELS / 2 : 0);
}

static int RegisterOffsetToOpIndex(uint32_t offset) {
    uint32_t baseoff = offset & 0xFF;
    uint32_t suboff = baseoff % 8;
    if (suboff >= 6) {
        return -1;
    }
    return suboff >= 3;
}

#define REG_FEEDCONN 0xC0
#define REG_CHARACTER 0x20
#define REG_LEVELS 0x40
#define REG_ATTACK 0x60
#define REG_SUSTAIN 0x80
#define REG_WAVE 0xE0
#define REG_FREQUENCY 0xA0
#define REG_NOTE 0xB0

// returns channel for note event or -1 if not a note event
static int IsNoteEvent(int addr) {
    int baseAddr = addr & 0xFF;
    if (baseAddr >= 0xB0 && baseAddr <= 0xB8) {
        return (baseAddr - 0xB0) * ((addr & 0x100) == 0 ? 1 : 2);
    }
    else if (baseAddr >= OPB_CMD_NOTEON && baseAddr < OPB_CMD_NOTEON + NUM_CHANNELS / 2) {
        return (baseAddr - OPB_CMD_NOTEON) * ((addr & 0x100) == 0 ? 1 : 2);
    }
    return -1;
}

static bool IsChannelNoteEvent(int addr, int channel) {
    return
        (addr == 0xB0 + (channel % 9) + (channel >= 9 ? 0x100 : 0)) ||
        (addr == OPB_CMD_NOTEON + (channel % 9) + (channel >= 9 ? 0x100 : 0));
}

static int ChannelFromRegister(int reg) {
    int baseReg = reg & 0xFF;
    if ((baseReg >= 0x20 && baseReg <= 0x95) || (baseReg >= 0xE0 && baseReg <= 0xF5)) {
        int offset = baseReg % 0x20;
        if (offset < 0 || offset >= 0x16) {
            return -1;
        }
        if ((reg & 0x100) != 0) {
            offset |= 0x100;
        }
        int ch;
        if ((ch = RegisterOffsetToChannel(offset)) >= 0) {
            return ch;
        }
    }
    else if ((baseReg >= 0xA0 && baseReg <= 0xB8) || (baseReg >= 0xC0 && baseReg <= 0xC8)) {
        int ch = baseReg % 0x10;
        if (ch < 0 || ch >= 0x9) {
            return -1;
        }
        if ((reg & 0x100) != 0) {
            ch += 9;
        }
        return ch;
    }
    return -1;
}

// 0 for modulator, 1 for carrier, -1 otherwise
static int RegisterToOpIndex(int reg) {
    int baseReg = reg & 0xFF;
    if ((baseReg >= 0x20 && baseReg <= 0x95) || (baseReg >= 0xE0 && baseReg <= 0xF5)) {
        int offset = baseReg % 0x20;
        if (offset < 0 || offset >= 0x16) {
            return -1;
        }
        int op;
        if ((op = RegisterOffsetToOpIndex(offset)) >= 0) {
            return op;
        }
    }
    return -1;
}

#ifndef OPB_READONLY
static WriteContext WriteContext_New(void) {
    WriteContext context = { 0 };

    context.CommandStream = Vector_New(sizeof(Command));
    context.Instruments = Vector_New(sizeof(OPB_Instrument));
    context.DataMap = Vector_New(sizeof(OpbData));
    for (int i = 0; i < NUM_TRACKS; i++) {
        context.Tracks[i] = Vector_New(sizeof(Command));
    }

    return context;
}

static bool CanCombineInstrument(OPB_Instrument* instr, Command* feedconn,
    Command* modChar, Command* modAttack, Command* modSustain, Command* modWave,
    Command* carChar, Command* carAttack, Command* carSustain, Command* carWave) {
    if ((feedconn == NULL || instr->FeedConn == feedconn->Data || instr->FeedConn < 0) &&
        (modChar == NULL || instr->Modulator.Characteristic == modChar->Data || instr->Modulator.Characteristic < 0) &&
        (modAttack == NULL || instr->Modulator.AttackDecay == modAttack->Data || instr->Modulator.AttackDecay < 0) &&
        (modSustain == NULL || instr->Modulator.SustainRelease == modSustain->Data || instr->Modulator.SustainRelease < 0) &&
        (modWave == NULL || instr->Modulator.WaveSelect == modWave->Data || instr->Modulator.WaveSelect < 0) &&
        (carChar == NULL || instr->Carrier.Characteristic == carChar->Data || instr->Carrier.Characteristic < 0) &&
        (carAttack == NULL || instr->Carrier.AttackDecay == carAttack->Data || instr->Carrier.AttackDecay < 0) &&
        (carSustain == NULL || instr->Carrier.SustainRelease == carSustain->Data || instr->Carrier.SustainRelease < 0) &&
        (carWave == NULL || instr->Carrier.WaveSelect == carWave->Data) || instr->Carrier.WaveSelect < 0) {
        instr->FeedConn = feedconn != NULL ? feedconn->Data : instr->FeedConn;
        instr->Modulator.Characteristic = modChar != NULL ? modChar->Data : instr->Modulator.Characteristic;
        instr->Modulator.AttackDecay = modAttack != NULL ? modAttack->Data : instr->Modulator.AttackDecay;
        instr->Modulator.SustainRelease = modSustain != NULL ? modSustain->Data : instr->Modulator.SustainRelease;
        instr->Modulator.WaveSelect = modWave != NULL ? modWave->Data : instr->Modulator.WaveSelect;
        instr->Carrier.Characteristic = carChar != NULL ? carChar->Data : instr->Carrier.Characteristic;
        instr->Carrier.AttackDecay = carAttack != NULL ? carAttack->Data : instr->Carrier.AttackDecay;
        instr->Carrier.SustainRelease = carSustain != NULL ? carSustain->Data : instr->Carrier.SustainRelease;
        instr->Carrier.WaveSelect = carWave != NULL ? carWave->Data : instr->Carrier.WaveSelect;
        return true;
    }
    return false;
}

static int GetInstrument(WriteContext* context, Command* feedconn,
    Command* modChar, Command* modAttack, Command* modSustain, Command* modWave,
    Command* carChar, Command* carAttack, Command* carSustain, Command* carWave, 
    OPB_Instrument* result
) {
    // find a matching instrument
    for (int i = 0; i < context->Instruments.Count; i++) {
        OPB_Instrument* instr = Vector_GetT(OPB_Instrument, &context->Instruments, i);
        if (instr == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        if (CanCombineInstrument(instr, feedconn, modChar, modAttack, modSustain, modWave, carChar, carAttack, carSustain, carWave)) {
            *result = *instr;
            return 0;
        }
    }

    // no instrument found, create and store new instrument
    OPB_Instrument instr = {
        feedconn == NULL ? -1 : feedconn->Data,
        {
            modChar == NULL ? -1 : modChar->Data,
            modAttack == NULL ? -1 : modAttack->Data,
            modSustain == NULL ? -1 : modSustain->Data,
            modWave == NULL ? -1 : modWave->Data,
        },
        {
            carChar == NULL ? -1 : carChar->Data,
            carAttack == NULL ? -1 : carAttack->Data,
            carSustain == NULL ? -1 : carSustain->Data,
            carWave == NULL ? -1 : carWave->Data,
        },
        (int)context->Instruments.Count
    };
    if (Vector_Add(&context->Instruments, &instr)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    *result = instr;
    return 0;
}

static int WriteInstrument(WriteContext* context, const OPB_Instrument* instr) {
    uint8_t feedConn = (uint8_t)(instr->FeedConn >= 0 ? instr->FeedConn : 0);
    uint8_t modChr = (uint8_t)(instr->Modulator.Characteristic >= 0 ? instr->Modulator.Characteristic : 0);
    uint8_t modAtk = (uint8_t)(instr->Modulator.AttackDecay >= 0 ? instr->Modulator.AttackDecay : 0);
    uint8_t modSus = (uint8_t)(instr->Modulator.SustainRelease >= 0 ? instr->Modulator.SustainRelease : 0);
    uint8_t modWav = (uint8_t)(instr->Modulator.WaveSelect >= 0 ? instr->Modulator.WaveSelect : 0);
    uint8_t carChr = (uint8_t)(instr->Carrier.Characteristic >= 0 ? instr->Carrier.Characteristic : 0);
    uint8_t carAtk = (uint8_t)(instr->Carrier.AttackDecay >= 0 ? instr->Carrier.AttackDecay : 0);
    uint8_t carSus = (uint8_t)(instr->Carrier.SustainRelease >= 0 ? instr->Carrier.SustainRelease : 0);
    uint8_t carWav = (uint8_t)(instr->Carrier.WaveSelect >= 0 ? instr->Carrier.WaveSelect : 0);

    WRITE(&feedConn, sizeof(uint8_t), 1, context);
    WRITE(&modChr, sizeof(uint8_t), 1, context);
    WRITE(&modAtk, sizeof(uint8_t), 1, context);
    WRITE(&modSus, sizeof(uint8_t), 1, context);
    WRITE(&modWav, sizeof(uint8_t), 1, context);
    WRITE(&carChr, sizeof(uint8_t), 1, context);
    WRITE(&carAtk, sizeof(uint8_t), 1, context);
    WRITE(&carSus, sizeof(uint8_t), 1, context);
    WRITE(&carWav, sizeof(uint8_t), 1, context);

    return 0;
}

static int WriteUint7(WriteContext* context, uint32_t value) {
    if (value >= 2097152) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = ((value & 0b0111111100000000000000) >> 14) | 0b10000000;
        uint8_t b3 = (value & 0b11111111000000000000000000000) >> 21;
        if (context->Write(&b0, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b1, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b2, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b3, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
    }
    else if (value >= 16384) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = (value & 0b0111111100000000000000) >> 14;
        if (context->Write(&b0, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b1, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b2, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
    }
    else if (value >= 128) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = (value & 0b011111110000000) >> 7;
        if (context->Write(&b0, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
        if (context->Write(&b1, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
    }
    else {
        uint8_t b0 = value & 0b01111111;
        if (context->Write(&b0, sizeof(uint8_t), 1, context->UserData) < 1) return -1;
    }
    return 0;
}

static int SeparateTracks(WriteContext* context) {
    for (int i = 0; i < context->CommandStream.Count; i++) {
        Command* cmd = Vector_GetT(Command, &context->CommandStream, i);
        if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        int channel = ChannelFromRegister(cmd->Addr);
        if (channel < 0) channel = NUM_TRACKS - 1;

        if (Vector_Add(&context->Tracks[channel], cmd)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    }
    return 0;
}

static int CountInstrumentChanges(Command* feedconn,
    Command* modChar, Command* modAttack, Command* modSustain, Command* modWave,
    Command* carChar, Command* carAttack, Command* carSustain, Command* carWave) {
    int count = 0;
    if (feedconn != NULL) count++;
    if (modChar != NULL) count++;
    if (modAttack != NULL) count++;
    if (modSustain != NULL) count++;
    if (modWave != NULL) count++;
    if (carChar != NULL) count++;
    if (carAttack != NULL) count++;
    if (carSustain != NULL) count++;
    if (carWave != NULL) count++;
    return count;
}

static int ProcessRange(WriteContext* context, int channel, double time, Command* commands, int cmdCount, Vector* range,
    int _debug_start, int _debug_end // these last two are only for logging in case of error
) {
    for (int i = 0; i < cmdCount; i++) {
        Command* cmd = commands + i;

        if (cmd->Time != time) {
            int timeMs = (int)(time * 1000);
            Log("A timing error occurred at %d ms on channel %d in range %d-%d\n", timeMs, channel, _debug_start, _debug_end);
            return OPBERR_LOGGED;
        }
    }

    Command* modChar = NULL, * modLevel = NULL, * modAttack = NULL, * modSustain = NULL, * modWave = NULL;
    Command* carChar = NULL, * carLevel = NULL, * carAttack = NULL, * carSustain = NULL, * carWave = NULL;
    Command* freq = NULL, * note = NULL, * feedconn = NULL;

    for (int i = 0; i < cmdCount; i++) {
        Command* cmd = commands + i;

        int baseAddr = cmd->Addr & 0xFF;
        int op;

        if ((op = RegisterToOpIndex(cmd->Addr)) > -1) {
            // command affects modulator or carrier
            if (op == 0) {
                if (baseAddr >= 0x20 && baseAddr <= 0x35)
                    modChar = cmd;
                else if (baseAddr >= 0x40 && baseAddr <= 0x55)
                    modLevel = cmd;
                else if (baseAddr >= 0x60 && baseAddr <= 0x75)
                    modAttack = cmd;
                else if (baseAddr >= 0x80 && baseAddr <= 0x95)
                    modSustain = cmd;
                else if (baseAddr >= 0xE0 && baseAddr <= 0xF5)
                    modWave = cmd;
            }
            else {
                if (baseAddr >= 0x20 && baseAddr <= 0x35)
                    carChar = cmd;
                else if (baseAddr >= 0x40 && baseAddr <= 0x55)
                    carLevel = cmd;
                else if (baseAddr >= 0x60 && baseAddr <= 0x75)
                    carAttack = cmd;
                else if (baseAddr >= 0x80 && baseAddr <= 0x95)
                    carSustain = cmd;
                else if (baseAddr >= 0xE0 && baseAddr <= 0xF5)
                    carWave = cmd;
            }
        }
        else {
            if (baseAddr >= 0xA0 && baseAddr <= 0xA8)
                freq = cmd;
            else if (baseAddr >= 0xB0 && baseAddr <= 0xB8) {
                if (note != NULL) {
                    int timeMs = (int)(time * 1000);
                    Log("A decoding error occurred at %d ms on channel %d in range %d-%d\n", timeMs, channel, _debug_start, _debug_end);
                    return OPBERR_LOGGED;
                }
                note = cmd;
            }
            else if (baseAddr >= 0xC0 && baseAddr <= 0xC8)
                feedconn = cmd;
            else {
                if (Vector_Add(range, cmd)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
            }
        }
    }

    // combine instrument data
    int instrChanges;
    if ((instrChanges = CountInstrumentChanges(feedconn, modChar, modAttack, modSustain, modWave, carChar, carAttack, carSustain, carWave)) > 0) {
        OPB_Instrument instr;
        int ret = GetInstrument(context, feedconn, modChar, modAttack, modSustain, modWave, carChar, carAttack, carSustain, carWave, &instr);
        if (ret) return ret;

        int size = Uint7Size((uint32_t)instr.Index) + 3;

        if (modLevel != NULL) {
            size++;
            instrChanges++;
        }
        if (carLevel != NULL) {
            size++;
            instrChanges++;
        }

        // combine with frequency and note command if present
        if (freq != NULL && note != NULL) {
            size += 2;
            instrChanges += 2;
        }

        if ((int)size < instrChanges * 2) {
            OpbData data = { 0 };
            OpbData_WriteUint7(&data, (uint32_t)instr.Index);

            uint8_t channelMask = channel |
                (modLevel != NULL ? 0b00100000 : 0) |
                (carLevel != NULL ? 0b01000000 : 0) |
                (feedconn != NULL ? 0b10000000 : 0);
            OpbData_WriteU8(&data, channelMask);

            int mask =
                (modChar != NULL ? 0b00000001 : 0) |
                (modAttack != NULL ? 0b00000010 : 0) |
                (modSustain != NULL ? 0b00000100 : 0) |
                (modWave != NULL ? 0b00001000 : 0) |
                (carChar != NULL ? 0b00010000 : 0) |
                (carAttack != NULL ? 0b00100000 : 0) |
                (carSustain != NULL ? 0b01000000 : 0) |
                (carWave != NULL ? 0b10000000 : 0);
            OpbData_WriteU8(&data, mask);

            // instrument command is 0xD0
            int reg = OPB_CMD_SETINSTRUMENT;

            if (freq != NULL && note != NULL) {
                OpbData_WriteU8(&data, freq->Data);
                OpbData_WriteU8(&data, note->Data);

                // play command is 0xD1
                reg = OPB_CMD_PLAYINSTRUMENT;
            }

            if (modLevel != NULL) OpbData_WriteU8(&data, modLevel->Data);
            if (carLevel != NULL) OpbData_WriteU8(&data, carLevel->Data);

            int opbIndex = (int32_t)context->DataMap.Count + 1;
            if (Vector_Add(&context->DataMap, &data)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);

            Command cmd = {
                (uint16_t)(reg + (channel >= 9 ? 0x100 : 0)), // register
                0, // data
                time,
                commands[0].OrderIndex,
                opbIndex
            };

            if (Vector_Add(range, &cmd)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
            feedconn = modChar = modLevel = modAttack = modSustain = modWave = carChar = carLevel = carAttack = carSustain = carWave = NULL;

            if (freq != NULL && note != NULL) {
                freq = note = NULL;
            }
        }
    }

    // combine frequency/note and modulator and carrier level data
    if (freq != NULL && note != NULL) {
        // note on command is 0xD7 through 0xDF (and 0x1D7 through 0x1DF for channels 10-18)
        int reg = OPB_CMD_NOTEON + (channel % 9) + (channel >= 9 ? 0x100 : 0);

        OpbData data = { 0 };
        OpbData_WriteU8(&data, freq->Data);

        int noteLevels = note->Data & 0b00111111;

        // encode modulator and carrier levels data in the note data's upper 2 (unused) bits
        if (modLevel != NULL) {
            noteLevels |= 0b01000000;
        }
        if (carLevel != NULL) {
            noteLevels |= 0b10000000;
        }

        OpbData_WriteU8(&data, noteLevels);

        if (modLevel != NULL) {
            OpbData_WriteU8(&data, modLevel->Data);
        }
        if (carLevel != NULL) {
            OpbData_WriteU8(&data, carLevel->Data);
        }

        int opbIndex = (int32_t)context->DataMap.Count + 1;
        if (Vector_Add(&context->DataMap, &data)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);

        Command cmd = {
            (uint16_t)reg, // register
            0, // data
            time,
            note->OrderIndex,
            opbIndex
        };

        if (Vector_Add(range, &cmd)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
        freq = note = modLevel = carLevel = NULL;
    }

    if (modChar != NULL) if (Vector_Add(range, modChar)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (modLevel != NULL) if (Vector_Add(range, modLevel)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (modAttack != NULL) if (Vector_Add(range, modAttack)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (modSustain != NULL) if (Vector_Add(range, modSustain)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (modWave != NULL) if (Vector_Add(range, modWave)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);

    if (carChar != NULL) if (Vector_Add(range, carChar)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (carLevel != NULL) if (Vector_Add(range, carLevel)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (carAttack != NULL) if (Vector_Add(range, carAttack)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (carSustain != NULL) if (Vector_Add(range, carSustain)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (carWave != NULL) if (Vector_Add(range, carWave)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);

    if (feedconn != NULL) if (Vector_Add(range, feedconn)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (freq != NULL) if (Vector_Add(range, freq)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    if (note != NULL) if (Vector_Add(range, note)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);

    return 0;
}

static int ProcessTrack(WriteContext* context, int channel, Vector* chOut) {
    Vector* commands = &context->Tracks[channel];

    if (commands->Count == 0) {
        return 0;
    }

    Command* cmd;

    cmd = Vector_GetT(Command, commands, 0);
    if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

    int lastOrder = cmd->OrderIndex;
    int i = 0;

    while (i < commands->Count) {
        cmd = Vector_GetT(Command, commands, i);
        if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        double time = cmd->Time;

        int start = i;

        // sequences must be all in the same time block and in order
        // sequences are capped by a note command (write to register B0-B8 or 1B0-1B8)
        Command* next = Vector_GetT(Command, commands, i);
        if (next == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        while (i < commands->Count && next->Time <= time && (next->OrderIndex - lastOrder) <= 1) {
            cmd = next;

            lastOrder = cmd->OrderIndex;
            i++;

            if (i < commands->Count) {
                next = Vector_GetT(Command, commands, i);
                if (next == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
            }

            if (IsChannelNoteEvent(cmd->Addr, channel)) {
                break;
            }
        }
        int end = i;

        VectorT(Command) range = Vector_New(sizeof(Command));
        Command* commandPtr = Vector_GetT(Command, commands, start);
        if (commandPtr == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        int ret = ProcessRange(context, channel, time, commandPtr, end - start, &range, start, end);
        if (ret) {
            Vector_Free(&range);
            return ret;
        }
        
        if (Vector_AddRange(chOut, range.Storage, range.Count)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
        Vector_Free(&range);

        if (i < commands->Count) {
            cmd = Vector_GetT(Command, commands, i);
            if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
            lastOrder = cmd->OrderIndex;
        }
    }

    return 0;
}

static int WriteChunk(WriteContext* context, double elapsed, int start, int count) {
    uint32_t elapsedMs = (uint32_t)((elapsed * 1000) + 0.5);
    int loCount = 0;
    int hiCount = 0;

    for (int i = start; i < start + count; i++) {
        Command* cmd = Vector_GetT(Command, &context->CommandStream, i);
        if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

        if ((cmd->Addr & 0x100) == 0) {
            loCount++;
        }
        else {
            hiCount++;
        }
    }

    // write header
    WRITE_UINT7(context, elapsedMs);
    WRITE_UINT7(context, loCount);
    WRITE_UINT7(context, hiCount);

    // write low and high register writes
    bool isLow = true;
    while (true) {
        for (int i = start; i < start + count; i++) {
            Command* cmd = Vector_GetT(Command, &context->CommandStream, i);
            if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

            if (((cmd->Addr & 0x100) == 0) == isLow) {
                uint8_t baseAddr = cmd->Addr & 0xFF;
                WRITE(&baseAddr, sizeof(uint8_t), 1, context);

                if (cmd->DataIndex) {
                    if (!IsSpecialCommand(baseAddr)) {
                        Log("Unexpected write error. Command had DataIndex but was not an OPB command\n");
                        return OPBERR_LOGGED;
                    }

                    // opb command
                    OpbData* data = Vector_GetT(OpbData, &context->DataMap, cmd->DataIndex - 1);
                    if (data == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
                    WRITE(data->Args, sizeof(uint8_t), data->Count, context);
                }
                else {
                    if (IsSpecialCommand(baseAddr)) {
                        Log("Unexpected write error. Command was an OPB command but had no DataIndex\n");
                        return OPBERR_LOGGED;
                    }

                    // regular write
                    WRITE(&(cmd->Data), sizeof(uint8_t), 1, context);
                }
            }
        }

        if (!isLow) {
            break;
        }

        isLow = !isLow;
    }

    return 0;
}

static int SortCommands(const void* a, const void* b) {
    return ((Command*)a)->OrderIndex - ((Command*)b)->OrderIndex;
}

static int ConvertToOpb(WriteContext* context) {
    if (context->Format < OPB_Format_Default || context->Format > OPB_Format_Raw) {
        context->Format = OPB_Format_Default;
    }

    WRITE(OPB_Header, sizeof(char), OPB_HEADER_SIZE, context);

    Log("OPB format %d (%s)\n", context->Format, OPB_GetFormatName(context->Format));

    uint8_t fmt = (uint8_t)context->Format;
    WRITE(&fmt, sizeof(uint8_t), 1, context);

    if (context->Format == OPB_Format_Raw) {
        Log("Writing raw OPL data stream\n");

        double lastTime = 0.0;
        for (int i = 0; i < context->CommandStream.Count; i++) {
            Command* cmd = Vector_GetT(Command, &context->CommandStream, i);
            if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

            uint16_t elapsed = FlipEndian16((uint16_t)((cmd->Time - lastTime) * 1000.0));
            uint16_t addr = FlipEndian16(cmd->Addr);

            WRITE(&elapsed, sizeof(uint16_t), 1, context);
            WRITE(&addr, sizeof(uint16_t), 1, context);
            WRITE(&(cmd->Data), sizeof(uint8_t), 1, context);
            lastTime = cmd->Time;
        }
        return 0;
    }

    // separate command stream into tracks
    Log("Separating OPL data stream into channels\n");
    int ret = SeparateTracks(context);
    if (ret) return ret;

    // process each track into its own output vector
    VectorT(Command) chOut[NUM_TRACKS];

    for (int i = 0; i < NUM_TRACKS; i++) {
        Log("Processing channel %d\n", i);
        chOut[i] = Vector_New(sizeof(Command));

        int ret = ProcessTrack(context, i, chOut + i);
        if (ret) {
            for (int j = 0; j < NUM_TRACKS; j++) {
                Vector_Free(chOut + j);
            }
            return ret;
        }
    }

    // combine all output back into command stream
    Log("Combining processed data into linear stream\n");
    Vector_Clear(&context->CommandStream, true);
    for (int i = 0; i < NUM_TRACKS; i++) {
        if (Vector_AddRange(&context->CommandStream, chOut[i].Storage, chOut[i].Count)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
    }

    for (int j = 0; j < NUM_TRACKS; j++) {
        Vector_Free(chOut + j);
    }

    // sort by received order
    Vector_Sort(&context->CommandStream, SortCommands);

    // write instruments table
    SEEK(context, 12, SEEK_CUR); // skip header

    Log("Writing instrument table\n");
    for (int i = 0; i < context->Instruments.Count; i++) {
        OPB_Instrument* instr = Vector_GetT(OPB_Instrument, &context->Instruments, i);
        if (instr == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
        int ret = WriteInstrument(context, instr);
        if (ret) return ret;
    }

    // write chunks
    {
        int chunks = 0;
        double lastTime = 0;
        int i = 0;

        Log("Writing chunks\n");
        while (i < context->CommandStream.Count) {
            Command* cmd = Vector_GetT(Command, &context->CommandStream, i);
            if (cmd == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
            double chunkTime = cmd->Time;

            Command* next = Vector_GetT(Command, &context->CommandStream, i);
            if (next == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);

            int start = i;
            while (i < context->CommandStream.Count && next->Time <= chunkTime) {
                i++;
                if (i < context->CommandStream.Count) {
                    next = Vector_GetT(Command, &context->CommandStream, i);
                    if (next == NULL) RET_LOG_VECTOR_OUT_OF_RANGE_ERR(OPBERR_VEC_INDEX_OUT_OF_RANGE);
                }
            }
            int end = i;

            int ret = WriteChunk(context, chunkTime - lastTime, start, end - start);
            if (ret) return ret;
            chunks++;

            lastTime = chunkTime;
        }

        // write header
        Log("Writing header\n");

        long fpos;
        TELL(context, fpos);

        uint32_t length = FlipEndian32(fpos);
        uint32_t instrCount = FlipEndian32((uint32_t)context->Instruments.Count);
        uint32_t chunkCount = FlipEndian32(chunks);

        SEEK(context, OPB_HEADER_SIZE + 1, SEEK_SET);
        WRITE(&length, sizeof(uint32_t), 1, context);
        WRITE(&instrCount, sizeof(uint32_t), 1, context);
        WRITE(&chunkCount, sizeof(uint32_t), 1, context);
    }

    return 0;
}

int OPB_OplToFile(OPB_Format format, OPB_Command* commandStream, size_t commandCount, const char* file) {
    FILE* outFile;
    if ((outFile = fopen(file, "wb")) == NULL) {
        Log("Couldn't open file '%s' for writing\n", file);
        return OPBERR_LOGGED;
    }
    int ret = OPB_OplToBinary(format, commandStream, commandCount, WriteToFile, SeekInFile, TellInFile, outFile);
    if (fclose(outFile)) {
        Log("Error while closing file '%s'\n", file);
        return OPBERR_LOGGED;
    }
    return ret;
}

int OPB_OplToBinary(OPB_Format format, OPB_Command* commandStream, size_t commandCount, OPB_StreamWriter write, OPB_StreamSeeker seek, OPB_StreamTeller tell, void* userData) {
    WriteContext context = WriteContext_New();

    context.Write = write;
    context.Seek = seek;
    context.Tell = tell;
    context.UserData = userData;
    context.Format = format;

    // convert stream to internal format
    int orderIndex = 0;
    for (int i = 0; i < commandCount; i++) {
        const OPB_Command* source = commandStream + i;

        if (IsSpecialCommand(source->Addr)) {
            Log("Illegal register 0x%03X with value 0x%02X in command stream, ignored\n", source->Addr, source->Data);
        }
        else {
            Command cmd = {
                source->Addr,   // OPL register
                source->Data,   // OPL data
                source->Time,   // Time in seconds
                orderIndex++,   // Stream index
                0               // Data index
            };

            if (Vector_Add(&context.CommandStream, &cmd)) RET_LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
        }
    }

    int ret = ConvertToOpb(&context);
    WriteContext_Free(&context);

    if (ret) {
        Log("%s\n", OPB_GetErrorMessage(ret));
    }

    return ret;
}
#endif

static int ReadInstrument(OPB_ReadContext* context, OPB_Instrument* instr, size_t index) {
    uint8_t buffer[9];
    READ(buffer, sizeof(uint8_t), 9, context);
    *instr = (OPB_Instrument) {
        buffer[0], // feedconn
        {
            buffer[1], // modulator characteristic
            buffer[2], // modulator attack/decay
            buffer[3], // modulator sustain/release
            buffer[4], // modulator wave select
        },
        {
            buffer[5], // carrier characteristic
            buffer[6], // carrier attack/decay
            buffer[7], // carrier sustain/release
            buffer[8], // carrier wave select
        },
        index // instrument index
    };
    return 0;
}

static int ReadUint7(OPB_ReadContext* context) {
    uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;

    if (context->Read(&b0, sizeof(uint8_t), 1, context->UserData) != 1) return -1;
    if (b0 >= 128) {
        b0 &= 0b01111111;
        if (context->Read(&b1, sizeof(uint8_t), 1, context->UserData) != 1) return -1;
        if (b1 >= 128) {
            b1 &= 0b01111111;
            if (context->Read(&b2, sizeof(uint8_t), 1, context->UserData) != 1) return -1;
            if (b2 >= 128) {
                b2 &= 0b01111111;
                if (context->Read(&b3, sizeof(uint8_t), 1, context->UserData) != 1) return -1;
            }
        }
    }

    return b0 | (b1 << 7) | (b2 << 14) | (b3 << 21);
}

static inline void AddToBuffer(OPB_ReadContext* context, OPB_Command cmd) {
    context->CommandBuffer[context->BufferCount++] = cmd;
}

static int ReadCommand(OPB_ReadContext* context) {
    int mask = context->CurrentChunk.Index >= context->CurrentChunk.LoCount ? 0x100 : 0x0;
    
    context->CurrentChunk.Index++;
    context->BufferCount = context->BufferIndex = 0;

    uint8_t baseAddr;
    READ(&baseAddr, sizeof(uint8_t), 1, context);

    int addr = baseAddr | mask;

    switch (baseAddr) {
        default: {
            uint8_t data;
            READ(&data, sizeof(uint8_t), 1, context);
            AddToBuffer(context, (OPB_Command){ (uint16_t)addr, data, context->Time });
            break;
        }
        
        case OPB_CMD_PLAYINSTRUMENT:
        case OPB_CMD_SETINSTRUMENT: {
            int instrIndex;
            READ_UINT7(instrIndex, context);

            uint8_t channelMask[2];
            READ(channelMask, sizeof(uint8_t), 2, context);

            int channel = channelMask[0];
            bool modLvl = (channel & 0b00100000) != 0;
            bool carLvl = (channel & 0b01000000) != 0;
            bool feedconn = (channel & 0b10000000) != 0;
            channel &= 0b00011111;

            if (channel < 0 || channel >= NUM_CHANNELS) {
#ifndef OPB_NOSTDLIB
                Log("Error reading OPB command: channel %d out of range\n", channel);
#else
                Log("Error reading OPB command: channel out of range\n");
#endif
                return OPBERR_LOGGED;
            }

            int chmask = channelMask[1];
            bool modChr = (chmask & 0b00000001) != 0;
            bool modAtk = (chmask & 0b00000010) != 0;
            bool modSus = (chmask & 0b00000100) != 0;
            bool modWav = (chmask & 0b00001000) != 0;
            bool carChr = (chmask & 0b00010000) != 0;
            bool carAtk = (chmask & 0b00100000) != 0;
            bool carSus = (chmask & 0b01000000) != 0;
            bool carWav = (chmask & 0b10000000) != 0;

            uint8_t freq = 0, note = 0;
            bool isPlay = baseAddr == OPB_CMD_PLAYINSTRUMENT;
            if (isPlay) {
                READ(&freq, sizeof(uint8_t), 1, context);
                READ(&note, sizeof(uint8_t), 1, context);
            }

            uint8_t modLvlData = 0, carLvlData = 0;
            if (modLvl) READ(&modLvlData, sizeof(uint8_t), 1, context);
            if (carLvl) READ(&carLvlData, sizeof(uint8_t), 1, context);

            if (instrIndex < 0 || instrIndex >= context->InstrumentCount) {
#ifndef OPB_NOSTDLIB
                Log("Error reading OPB command: instrument %d out of range\n", instrIndex);
#else
                Log("Error reading OPB command: instrument out of range\n");
#endif
                return OPBERR_LOGGED;
            }

            OPB_Instrument* instr = &context->Instruments[instrIndex];
            int conn = ChannelToOffset[channel];
            int mod = OperatorOffsets[ChannelToOp[channel]];
            int car = mod + 3;
            int playOffset = ChannelToOffset[channel];

            if (feedconn) AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_FEEDCONN + conn), (uint8_t)instr->FeedConn, context->Time });
            if (modChr)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_CHARACTER + mod), (uint8_t)instr->Modulator.Characteristic, context->Time });
            if (modLvl)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_LEVELS + mod), modLvlData, context->Time });
            if (modAtk)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_ATTACK + mod), (uint8_t)instr->Modulator.AttackDecay, context->Time });
            if (modSus)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_SUSTAIN + mod), (uint8_t)instr->Modulator.SustainRelease, context->Time });
            if (modWav)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_WAVE + mod), (uint8_t)instr->Modulator.WaveSelect, context->Time });
            if (carChr)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_CHARACTER + car), (uint8_t)instr->Carrier.Characteristic, context->Time });
            if (carLvl)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_LEVELS + car), carLvlData, context->Time });
            if (carAtk)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_ATTACK + car), (uint8_t)instr->Carrier.AttackDecay, context->Time });
            if (carSus)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_SUSTAIN + car), (uint8_t)instr->Carrier.SustainRelease, context->Time });
            if (carWav)   AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_WAVE + car), (uint8_t)instr->Carrier.WaveSelect, context->Time });
            if (isPlay) {
                AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_FREQUENCY + playOffset), freq, context->Time });
                AddToBuffer(context, (OPB_Command){ (uint16_t)(REG_NOTE + playOffset), note, context->Time });
            }

            break;
        }

        case OPB_CMD_NOTEON:
        case OPB_CMD_NOTEON + 1:
        case OPB_CMD_NOTEON + 2:
        case OPB_CMD_NOTEON + 3:
        case OPB_CMD_NOTEON + 4:
        case OPB_CMD_NOTEON + 5:
        case OPB_CMD_NOTEON + 6:
        case OPB_CMD_NOTEON + 7:
        case OPB_CMD_NOTEON + 8: {
            int channel = (baseAddr - 0xD7) + (mask != 0 ? 9 : 0);

            if (channel < 0 || channel >= NUM_CHANNELS) {
#ifndef OPB_NOSTDLIB
                Log("Error reading OPB command: channel %d out of range\n", channel);
#else
                Log("Error reading OPB command: channel out of range\n");
#endif
                return OPBERR_LOGGED;
            }

            uint8_t freqNote[2];
            READ(freqNote, sizeof(uint8_t), 2, context);

            uint8_t freq = freqNote[0];
            uint8_t note = freqNote[1];

            AddToBuffer(context, (OPB_Command){ (uint16_t)(addr - (OPB_CMD_NOTEON - REG_FREQUENCY)), freq, context->Time });
            AddToBuffer(context, (OPB_Command){ (uint16_t)(addr - (OPB_CMD_NOTEON - REG_NOTE)), (uint8_t)(note & 0b00111111), context->Time });

            if ((note & 0b01000000) != 0) {
                // set modulator volume
                uint8_t vol;
                READ(&vol, sizeof(uint8_t), 1, context);
                int reg = REG_LEVELS + OperatorOffsets[ChannelToOp[channel]];
                AddToBuffer(context, (OPB_Command){ (uint16_t)reg, vol, context->Time });
            }
            if ((note & 0b10000000) != 0) {
                // set carrier volume
                uint8_t vol;
                READ(&vol, sizeof(uint8_t), 1, context);
                int reg = REG_LEVELS + 3 + OperatorOffsets[ChannelToOp[channel]];
                AddToBuffer(context, (OPB_Command){ (uint16_t)reg, vol, context->Time });
            }
            break;
        }
    }

    return 0;
}

static int ReadChunk(OPB_ReadContext* context, bool* success) {
    *success = false;
    if (context->ChunkIndex >= context->ChunkCount) {
        return 0;
    }

    int elapsed, loCount, hiCount;

    READ_UINT7(elapsed, context);
    READ_UINT7(loCount, context);
    READ_UINT7(hiCount, context);

    context->CurrentChunk.LoCount = loCount;
    context->CurrentChunk.Count = (size_t)loCount + hiCount;

    context->CurrentChunk.Index = 0;
    context->ChunkIndex++;

    context->Time += elapsed / 1000.0;

    *success = true;
    return 0;
}

static int ReadOpbHeader(OPB_ReadContext* context) {
    char id[OPB_HEADER_SIZE + 1] = { 0 };
    READ(id, sizeof(char), OPB_HEADER_SIZE, context);

    if (id[0] != 'O' || id[1] != 'P' || id[2] != 'B' || id[3] != 'i' || id[4] != 'n') {
        return OPBERR_NOT_AN_OPB_FILE;
    }
    
    switch (id[5]) {
    case '1':
        break;
    default:
        return OPBERR_VERSION_UNSUPPORTED;
    }

    if (id[6] != '\0') {
        return OPBERR_NOT_AN_OPB_FILE;
    }

    uint8_t fmt;
    READ(&fmt, sizeof(uint8_t), 1, context);
    context->Format = fmt;

    switch (context->Format) {
    default:
#ifndef OPB_NOSTDLIB
        Log("Error reading OPB file: unknown format %d\n", fmt);
#else
        Log("Error reading OPB file: unknown format\n");
#endif
        return OPBERR_LOGGED;

    case OPB_Format_Raw:
        context->ChunkDataOffset = OPB_HEADER_SIZE + 1;
        break;

    case OPB_Format_Default: {
        uint32_t header[3];
        READ(header, sizeof(uint32_t), 3, context);
        for (int i = 0; i < 3; i++) header[i] = FlipEndian32(header[i]);

        context->SizeBytes = header[0];
        context->InstrumentCount = header[1];
        context->ChunkCount = header[2];
        context->ChunkDataOffset = OPB_DATA_START + context->InstrumentCount * OPB_INSTRUMENT_SIZE;
        break;
    }
    }

    return 0;
}

#define RAW_ENTRY_SIZE 5

static bool ReadRawEntry(OPB_ReadContext* context, uint16_t* elapsed, uint16_t* addr, uint8_t* data) {
    uint8_t buffer[RAW_ENTRY_SIZE];
    if (context->Read(buffer, 1, RAW_ENTRY_SIZE, context->UserData) < RAW_ENTRY_SIZE) {
        return false;
    }

    *elapsed = ((uint16_t)buffer[0] << 8) | (uint16_t)buffer[1];
    *addr = ((uint16_t)buffer[2] << 8) | (uint16_t)buffer[3];
    *data = buffer[4];

    return true;
}

static int ReadCommands(OPB_ReadContext* context, OPB_Command* buffer, int maxCommands, int* commandsRead) {
    *commandsRead = 0;
    int ret;

    if (!context->InstrumentsInitialized) {
        context->InstrumentsInitialized = true;

        // read instruments
        if (context->Format != OPB_Format_Raw) {
#ifndef OPB_NOSTDLIB
            context->Instruments = calloc(context->InstrumentCount, sizeof(OPB_Instrument));
            context->InstrumentCapacity = context->InstrumentCount;
            context->FreeInstruments = true;

            if (context->Instruments == NULL) {
                return OPBERR_OUT_OF_MEMORY;
            }
#else
            if (context->Instruments == NULL) {
                return OPBERR_NO_INSTRUMENT_BUFFER;
            }
#endif

            if (context->InstrumentCapacity < context->InstrumentCount) {
                return OPBERR_INSTRUMENT_BUFFER_SIZE_OVERFLOW;
            }

            // these should only be NULL if we're coming from OPB_BinaryToOpl
            bool canSeek = context->Tell != NULL && context->Seek != NULL;
            
            long offset = 0;
            if (canSeek) {
                offset = context->Tell(context->UserData);
                if (offset == -1L) return OPBERR_TELL_ERROR;
                if (context->Seek(context->UserData, OPB_DATA_START, SEEK_SET)) return OPBERR_SEEK_ERROR;
            }

            for (size_t i = 0; i < context->InstrumentCount; i++) {
                ret = ReadInstrument(context, &context->Instruments[i], i);
                if (ret) return ret;
            }

            if (canSeek) {
                if (offset < context->ChunkDataOffset) offset = (long)context->ChunkDataOffset;
                if (context->Seek(context->UserData, offset, SEEK_SET)) return OPBERR_SEEK_ERROR;
            }
        }
    }

    int index = 0;

    if (context->Format == OPB_Format_Raw) {
        while (index < maxCommands) {
            uint16_t elapsed, addr;
            uint8_t data;

            if (!ReadRawEntry(context, &elapsed, &addr, &data)) {
                break;
            }

            context->Time += elapsed / 1000.0;
            buffer[index++] = (OPB_Command){ addr, data, context->Time };
        }
    }
    else {
        while (index < maxCommands) {
            // empty command buffer first
            // this command buffer is used because special OPB commands generate
            // multiple OPL commands in one go
            if (context->BufferIndex < context->BufferCount) {
                buffer[index++] = context->CommandBuffer[context->BufferIndex++];
            }
            else {
                if (context->CurrentChunk.Index >= context->CurrentChunk.Count) {
                    // read chunk header
                    bool success;
                    ret = ReadChunk(context, &success);
                    if (ret) return ret;

                    if (!success) {
                        break;
                    }
                }
                else {
                    // read command into command buffer
                    ret = ReadCommand(context);
                    if (ret) return ret;
                }
            }
        }
    }

    *commandsRead = index;
    return 0;
}

int OPB_ReadBuffer(OPB_File* opb, OPB_Command* buffer, int count, int* errorCode) {
    if (opb == NULL) {
        *errorCode = OPBERR_NULL_INSTANCE;
        return 0;
    }
    if (opb->disposedValue) {
        *errorCode = OPBERR_DISPOSED;
        return 0;
    }

    if (buffer == NULL) {
        *errorCode = OPBERR_INVALID_BUFFER;
        return 0;
    }

    int commandsRead;
    *errorCode = ReadCommands(&opb->context, buffer, count, &commandsRead);

    if (*errorCode) {
        // this should be zero when returning from ReadCommands with an error
        // but an extra check for safety can't hurt
        commandsRead = 0;
    }

    return commandsRead;
}

#ifndef OPB_NOSTDLIB
OPB_Command* OPB_ReadToEnd(OPB_File* opb, size_t* count, int* errorCode) {
    if (opb == NULL) {
        *errorCode = OPBERR_NULL_INSTANCE;
        return NULL;
    }
    if (opb->disposedValue) {
        *errorCode = OPBERR_DISPOSED;
        return NULL;
    }

    VectorT(OPB_Command) result = Vector_New(sizeof(OPB_Command));
    #define OPB_READTOEND_BUFSIZE 32
    OPB_Command buffer[OPB_READTOEND_BUFSIZE] = { 0 };

    int itemsRead;
    while ((itemsRead = OPB_ReadBuffer(opb, buffer, OPB_READTOEND_BUFSIZE, errorCode)) > 0) {
        if (Vector_AddRange(&result, buffer, itemsRead)) {
            LOG_VECTOR_GENERIC_ERR(OPBERR_VECTOR_ERROR);
            *errorCode = OPBERR_VECTOR_ERROR;
            break;
        }
    }

    if (*errorCode) {
        Vector_Free(&result);
    }

    *count = result.Count;
    return result.Storage;
}
#endif

int OPB_Reset(OPB_File* opb) {
    if (opb == NULL) {
        return OPBERR_NULL_INSTANCE;
    }
    if (opb->disposedValue) {
        return OPBERR_DISPOSED;
    }

    opb->context.BufferCount = 0;
    opb->context.ChunkIndex = 0;
    opb->context.Time = 0;
    opb->context.CurrentChunk = (OPB_Chunk){ 0 };

    return opb->context.Seek(opb->context.UserData, (long)opb->context.ChunkDataOffset, SEEK_SET);
}

void OPB_Free(OPB_File* opb) {
    if (opb != NULL && !opb->disposedValue) {
        ReadContext_Free(&opb->context);
        opb->disposedValue = true;
    }
}

OPB_HeaderInfo OPB_GetHeaderInfo(OPB_File* opb) {
    OPB_HeaderInfo info = { 0 };
    if (opb != NULL) {
        info.Format = opb->context.Format;
        info.SizeBytes = opb->context.SizeBytes;
        info.InstrumentCount = opb->context.InstrumentCount;
        info.ChunkCount = opb->context.ChunkCount;
    }
    return info;
}

int OPB_OpenStream(OPB_StreamReader read, OPB_StreamSeeker seek, OPB_StreamTeller tell, void* userData, OPB_File* opb) {
    OPB_File empty = { 0 };
    *opb = empty;
    opb->context.Read = read;
    opb->context.Seek = seek;
    opb->context.Tell = tell;
    opb->context.UserData = userData;
    return ReadOpbHeader(&opb->context);
}

#ifndef OPB_NOSTDLIB
int OPB_OpenFile(const char* filename, OPB_File* opb) {
    FILE* inFile;
    if ((inFile = fopen(filename, "rb")) == NULL) {
        Log("Couldn't open file '%s' for reading\n", filename);
        return OPBERR_LOGGED;
    }

    OPB_OpenStream(ReadFromFile, SeekInFile, TellInFile, inFile, opb);
    opb->context.UserDataDisposeType = USERDATA_DISPOSE_FCLOSE;
    return 0;
}

int OPB_FileToOpl(const char* file, OPB_BufferReceiver receiver, void* receiverData) {
    OPB_File opb;
    int ret = OPB_OpenFile(file, &opb);
    if (ret) return ret;

    OPB_Command buffer[64] = { 0 };

    int errorCode;
    int count;
    while ((count = OPB_ReadBuffer(&opb, buffer, 64, &errorCode)) > 0) {
        int ret = receiver(buffer, count, receiverData);
        if (ret) {
            errorCode = OPBERR_BUFFER_ERROR;
            break;
        }
    }

    OPB_Free(&opb);
    return errorCode;
}
#endif

int OPB_ProvideInstrumentBuffer(OPB_File* opb, OPB_Instrument* buffer, size_t capacity) {
    if (opb == NULL) {
        return OPBERR_NULL_INSTANCE;
    }
    if (opb->disposedValue) {
        return OPBERR_DISPOSED;
    }
    if (opb->context.InstrumentsInitialized) {
        return OPBERR_INSTRUMENT_BUFFER_ERROR;
    }
    if (opb->context.InstrumentCount > capacity) {
        return OPBERR_INSTRUMENT_BUFFER_ERROR;
    }
    opb->context.Instruments = buffer;
    opb->context.InstrumentCapacity = capacity;
    opb->context.FreeInstruments = false;
    return 0;
}

// deprecated, kept for backward compatibility
int OPB_BinaryToOpl(OPB_StreamReader reader, void* readerData, OPB_BufferReceiver receiver, void* receiverData) {
    OPB_File opb;
    OPB_OpenStream(reader, NULL, readerData, NULL, &opb);

    OPB_Command buffer[64] = { 0 };

    int errorCode;
    int count;
    while ((count = OPB_ReadBuffer(&opb, buffer, 64, &errorCode)) > 0) {
        int ret = receiver(buffer, count, receiverData);
        if (ret) {
            errorCode = OPBERR_BUFFER_ERROR;
            break;
        }
    }

    OPB_Free(&opb);
    return errorCode;
}

const char* OPB_GetErrorMessage(int errCode) {
    switch (errCode) {
    case OPBERR_LOGGED:
        return "OPB error was logged";
    case OPBERR_WRITE_ERROR:
        return "A write error occurred while converting OPB";
    case OPBERR_SEEK_ERROR:
        return "A seek error occurred while converting OPB";
    case OPBERR_TELL_ERROR:
        return "A file position error occurred while converting OPB";
    case OPBERR_READ_ERROR:
        return "A read error occurred while converting OPB";
    case OPBERR_BUFFER_ERROR:
        return "A buffer error occurred while converting OPB";
    case OPBERR_NOT_AN_OPB_FILE:
        return "Couldn't parse OPB file; not a valid OPB file";
    case OPBERR_VERSION_UNSUPPORTED:
        return "Couldn't parse OPB file; invalid version or version unsupported";
    case OPBERR_OUT_OF_MEMORY:
        return "Out of memory";
    case OPBERR_DISPOSED:
        return "Couldn't perform OPB_File operation; OPB_File instance was freed";
    case OPBERR_INVALID_BUFFER:
        return "Argument \"buffer\" cannot be NULL";
    case OPBERR_NO_INSTRUMENT_BUFFER:
        return "No instrument buffer was supplied and calloc was disabled";
    case OPBERR_INSTRUMENT_BUFFER_SIZE_OVERFLOW:
        return "The supplied instrument buffer's capacity was insufficient to hold all items";
    case OPBERR_VECTOR_ERROR:
        return "There was an error in the Vector type";
    case OPBERR_VEC_INDEX_OUT_OF_RANGE:
        return "Index out of range error in Vector";
    case OPBERR_NULL_INSTANCE:
        return "OPB_File instance was NULL";
    case OPBERR_INSTRUMENT_BUFFER_ERROR:
        return "OPB_File instance's instrument buffer was already initialized";
    case OPBERR_INSTRUMENT_BUFFER_SIZE:
        return "Instrument buffer supplied to OPB_ProvideInstrumentBuffer was not large enough to hold all instruments";
    default:
        return "Unknown OPB error";
    }
}