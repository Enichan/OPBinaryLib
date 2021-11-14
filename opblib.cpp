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
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include "opblib.h"

#define CONCAT_IMPL(x, y) x##y
#define MACRO_CONCAT(x, y) CONCAT_IMPL(x, y)

#define NUM_CHANNELS 18
#define NUM_TRACKS (NUM_CHANNELS + 1)

#define WRITE(buffer, size, count, context) \
    if (context.Write(buffer, size, count, context.UserData) != count) return OPBERR_WRITE_ERROR
#define WRITE_UINT7(context, value) \
    if (WriteUint7(context, value)) return OPBERR_WRITE_ERROR

#define SEEK(context, offset, origin) \
    if (context.Seek(context.UserData, offset, origin)) return OPBERR_SEEK_ERROR

#define READ(buffer, size, count, context) \
    if (context.Read(buffer, size, count, context.UserData) != count) return OPBERR_READ_ERROR
#define READ_UINT7(var, context) \
    if ((var = ReadUint7(context)) < 0) return OPBERR_READ_ERROR

#define SUBMIT(stream, count, context) \
    if (context.Submit(stream, count, context.ReceiverData)) return OPBERR_BUFFER_ERROR

typedef struct Context Context;
typedef struct Command Command;
typedef struct OpbData OpbData;
typedef struct Instrument Instrument;

typedef struct Context {
// msvc whining about unscoped enums ugghhh
#pragma warning(push)
#pragma warning(disable : 26812)
    Context() : Write(NULL), Seek(NULL), Tell(NULL), Read(NULL), Submit(NULL), UserData(NULL), ReceiverData(NULL), Format(OPB_Format_Default), Time(0) {}
#pragma warning(pop)

    std::vector<Command> CommandStream;
    OPB_StreamWriter Write;
    OPB_StreamSeeker Seek;
    OPB_StreamTeller Tell;
    OPB_StreamReader Read;
    OPB_BufferReceiver Submit;
    OPB_Format Format;
    std::map<int, OpbData> DataMap;
    std::vector<Instrument> Instruments;
    std::vector<Command> Tracks[NUM_TRACKS];
    double Time;
    void* UserData;
    void* ReceiverData;
} Context;

OPB_LogHandler OPB_Log;

#define LOG(value) \
if (OPB_Log) { \
    std::stringstream MACRO_CONCAT(__ss, __LINE__); \
    MACRO_CONCAT(__ss, __LINE__) << value; \
    OPB_Log(MACRO_CONCAT(__ss, __LINE__).str().c_str()); \
} \

static std::string GetFormatName(OPB_Format fmt) {
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

typedef struct OpbData {
    uint32_t Count;
    uint8_t Args[16];

    void WriteUint7(uint32_t value) {
        if (value >= 2097152) {
            uint8_t b0 = (value & 0b01111111) | 0b10000000;
            uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
            uint8_t b2 = ((value & 0b0111111100000000000000) >> 14) | 0b10000000;
            uint8_t b3 = ((value & 0b11111111000000000000000000000) >> 21);
            Args[Count] = b0; Count++;
            Args[Count] = b1; Count++;
            Args[Count] = b2; Count++;
            Args[Count] = b3; Count++;
        }
        else if (value >= 16384) {
            uint8_t b0 = (value & 0b01111111) | 0b10000000;
            uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
            uint8_t b2 = (value & 0b0111111100000000000000) >> 14;
            Args[Count] = b0; Count++;
            Args[Count] = b1; Count++;
            Args[Count] = b2; Count++;
        }
        else if (value >= 128) {
            uint8_t b0 = (value & 0b01111111) | 0b10000000;
            uint8_t b1 = (value & 0b011111110000000) >> 7;
            Args[Count] = b0; Count++;
            Args[Count] = b1; Count++;
        }
        else {
            uint8_t b0 = value & 0b01111111;
            Args[Count] = b0; Count++;
        }
    }

    void WriteU8(uint32_t value) {
        Args[Count] = (uint8_t)value;
        Count++;
    }
} OpbData;

#define OPB_CMD_SETINSTRUMENT 0xD0
#define OPB_CMD_PLAYINSTRUMENT 0xD1
#define OPB_CMD_NOTEON 0xD7

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

static std::map<int, int> RegisterOffsetToChannel = {
    { 0x00, 0 }, { 0x03, 0 },
    { 0x01, 1 }, { 0x04, 1 },
    { 0x02, 2 }, { 0x05, 2 },
    { 0x08, 3 }, { 0x0B, 3 },
    { 0x09, 4 }, { 0x0C, 4 },
    { 0x0A, 5 }, { 0x0D, 5 },
    { 0x10, 6 }, { 0x13, 6 },
    { 0x11, 7 }, { 0x14, 7 },
    { 0x12, 8 }, { 0x15, 8 },
    { 0x100, 9 }, { 0x103, 9 },
    { 0x101, 10 }, { 0x104, 10 },
    { 0x102, 11 }, { 0x105, 11 },
    { 0x108, 12 }, { 0x10B, 12 },
    { 0x109, 13 }, { 0x10C, 13 },
    { 0x10A, 14 }, { 0x10D, 14 },
    { 0x110, 15 }, { 0x113, 15 },
    { 0x111, 16 }, { 0x114, 16 },
    { 0x112, 17 }, { 0x115, 17 },
};

static std::map<int, int> RegisterOffsetToOpIndex = {
    { 0x00, 0 }, { 0x03, 1 },
    { 0x01, 0 }, { 0x04, 1 },
    { 0x02, 0 }, { 0x05, 1 },
    { 0x08, 0 }, { 0x0B, 1 },
    { 0x09, 0 }, { 0x0C, 1 },
    { 0x0A, 0 }, { 0x0D, 1 },
    { 0x10, 0 }, { 0x13, 1 },
    { 0x11, 0 }, { 0x14, 1 },
    { 0x12, 0 }, { 0x15, 1 },
    { 0x100, 0 }, { 0x103, 1 },
    { 0x101, 0 }, { 0x104, 1 },
    { 0x102, 0 }, { 0x105, 1 },
    { 0x108, 0 }, { 0x10B, 1 },
    { 0x109, 0 }, { 0x10C, 1 },
    { 0x10A, 0 }, { 0x10D, 1 },
    { 0x110, 0 }, { 0x113, 1 },
    { 0x111, 0 }, { 0x114, 1 },
    { 0x112, 0 }, { 0x115, 1 },
};

#define REG_FEEDCONN 0xC0
#define REG_CHARACTER 0x20
#define REG_LEVELS 0x40
#define REG_ATTACK 0x60
#define REG_SUSTAIN 0x80
#define REG_WAVE 0xE0
#define REG_FREQUENCY 0xA0
#define REG_NOTE 0xB0

typedef struct Operator {
    int16_t Characteristic;
    int16_t AttackDecay;
    int16_t SustainRelease;
    int16_t WaveSelect;

    bool operator==(const Operator& o) const {
        return Characteristic == o.Characteristic && AttackDecay == o.AttackDecay &&
            SustainRelease == o.SustainRelease && WaveSelect == o.WaveSelect;
    }

    bool operator<(const Operator& o)  const {
        return
            (Characteristic < o.Characteristic) ||
            (Characteristic == o.Characteristic && AttackDecay < o.AttackDecay) ||
            (Characteristic == o.Characteristic && AttackDecay == o.AttackDecay && SustainRelease < o.SustainRelease) ||
            (Characteristic == o.Characteristic && AttackDecay == o.AttackDecay && SustainRelease == o.SustainRelease && WaveSelect < o.WaveSelect);
    }
} Operator;

typedef struct Instrument {
    int16_t FeedConn;
    Operator Modulator;
    Operator Carrier;
    int Index;

    bool operator==(const Instrument& o) const {
        return FeedConn == o.FeedConn && Modulator == o.Modulator && Carrier == o.Carrier;
    }

    bool operator<(const Instrument& o)  const {
        return
            (FeedConn < o.FeedConn) ||
            (FeedConn == o.FeedConn && Modulator < o.Modulator) ||
            (FeedConn == o.FeedConn && Modulator == o.Modulator && Carrier < o.Carrier);
    }

    bool IsComplete() {
        return FeedConn > -1 &&
            Modulator.Characteristic > -1 && Modulator.AttackDecay > -1 && Modulator.SustainRelease > -1 && Modulator.WaveSelect > -1 &&
            Carrier.Characteristic > -1 && Carrier.AttackDecay > -1 && Carrier.SustainRelease > -1 && Carrier.WaveSelect > -1;
    }
} Instrument;

static Instrument GetInstrument(Context& context, Command* feedconn,
    Command* modChar, Command* modAttack, Command* modSustain, Command* modWave,
    Command* carChar, Command* carAttack, Command* carSustain, Command* carWave) {
    // find a matching instrument
    for (int i = 0; i < context.Instruments.size(); i++) {
        const Instrument& instr = context.Instruments[i];

        if ((feedconn == NULL || instr.FeedConn == feedconn->Data) &&
            (modChar == NULL || instr.Modulator.Characteristic == modChar->Data) &&
            (modAttack == NULL || instr.Modulator.AttackDecay == modAttack->Data) &&
            (modSustain == NULL || instr.Modulator.SustainRelease == modSustain->Data) &&
            (modWave == NULL || instr.Modulator.WaveSelect == modWave->Data) &&
            (carChar == NULL || instr.Carrier.Characteristic == carChar->Data) &&
            (carAttack == NULL || instr.Carrier.AttackDecay == carAttack->Data) &&
            (carSustain == NULL || instr.Carrier.SustainRelease == carSustain->Data) &&
            (carWave == NULL || instr.Carrier.WaveSelect == carWave->Data)) {
            return instr;
        }
    }

    // no instrument found, create and store new instrument
    Instrument instr = {
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
        (int32_t)context.Instruments.size()
    };
    context.Instruments.push_back(instr);
    return instr;
}

static int WriteInstrument(Context& context, const Instrument& instr) {
    uint8_t feedConn = (uint8_t)(instr.FeedConn >= 0 ? instr.FeedConn : 0);
    uint8_t modChr = (uint8_t)(instr.Modulator.Characteristic >= 0 ? instr.Modulator.Characteristic : 0);
    uint8_t modAtk = (uint8_t)(instr.Modulator.AttackDecay >= 0 ? instr.Modulator.AttackDecay : 0);
    uint8_t modSus = (uint8_t)(instr.Modulator.SustainRelease >= 0 ? instr.Modulator.SustainRelease : 0);
    uint8_t modWav = (uint8_t)(instr.Modulator.WaveSelect >= 0 ? instr.Modulator.WaveSelect : 0);
    uint8_t carChr = (uint8_t)(instr.Carrier.Characteristic >= 0 ? instr.Carrier.Characteristic : 0);
    uint8_t carAtk = (uint8_t)(instr.Carrier.AttackDecay >= 0 ? instr.Carrier.AttackDecay : 0);
    uint8_t carSus = (uint8_t)(instr.Carrier.SustainRelease >= 0 ? instr.Carrier.SustainRelease : 0);
    uint8_t carWav = (uint8_t)(instr.Carrier.WaveSelect >= 0 ? instr.Carrier.WaveSelect : 0);

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

static int WriteUint7(Context& context, uint32_t value) {
    if (value >= 2097152) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = ((value & 0b0111111100000000000000) >> 14) | 0b10000000;
        uint8_t b3 = ((value & 0b11111111000000000000000000000) >> 21);
        WRITE(&b0, sizeof(uint8_t), 1, context);
        WRITE(&b1, sizeof(uint8_t), 1, context);
        WRITE(&b2, sizeof(uint8_t), 1, context);
        WRITE(&b3, sizeof(uint8_t), 1, context);
    }
    else if (value >= 16384) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = ((value & 0b011111110000000) >> 7) | 0b10000000;
        uint8_t b2 = (value & 0b0111111100000000000000) >> 14;
        WRITE(&b0, sizeof(uint8_t), 1, context);
        WRITE(&b1, sizeof(uint8_t), 1, context);
        WRITE(&b2, sizeof(uint8_t), 1, context);
    }
    else if (value >= 128) {
        uint8_t b0 = (value & 0b01111111) | 0b10000000;
        uint8_t b1 = (value & 0b011111110000000) >> 7;
        WRITE(&b0, sizeof(uint8_t), 1, context);
        WRITE(&b1, sizeof(uint8_t), 1, context);
    }
    else {
        uint8_t b0 = value & 0b01111111;
        WRITE(&b0, sizeof(uint8_t), 1, context);
    }
    return 0;
}

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
        if (RegisterOffsetToChannel.count(offset)) {
            return RegisterOffsetToChannel[offset];
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
        if (RegisterOffsetToOpIndex.count(offset)) {
            return RegisterOffsetToOpIndex[offset];
        }
    }
    return -1;
}

static void SeparateTracks(Context& context) {
    for (int i = 0; i < context.CommandStream.size(); i++) {
        const Command& cmd = context.CommandStream[i];

        int channel = ChannelFromRegister(cmd.Addr);
        if (channel < 0) channel = NUM_TRACKS - 1;

        context.Tracks[channel].push_back(cmd);
    }
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

static int ProcessRange(Context& context, int channel, double time, std::vector<Command> commands, std::vector<Command>& range, int start, int end) {
    for (const auto& cmd : commands) {
        if (cmd.Time != time) {
            int timeMs = (int)(time * 1000);
            LOG("A timing error occurred at " << timeMs << "ms on channel " << channel << " in range " << start << "-" << end << "\n");
            return OPBERR_LOGGED;
        }
    }

    Command* modChar = NULL, * modLevel = NULL, * modAttack = NULL, * modSustain = NULL, * modWave = NULL;
    Command* carChar = NULL, * carLevel = NULL, * carAttack = NULL, * carSustain = NULL, * carWave = NULL;
    Command* freq = NULL, * note = NULL, * feedconn = NULL;

    for (int i = 0; i < commands.size(); i++) {
        const Command& cmd = commands[i];

        int baseAddr = cmd.Addr & 0xFF;
        int op;

        if ((op = RegisterToOpIndex(cmd.Addr)) > -1) {
            // command affects modulator or carrier
            if (op == 0) {
                if (baseAddr >= 0x20 && baseAddr <= 0x35)
                    modChar = &commands[i];
                else if (baseAddr >= 0x40 && baseAddr <= 0x55)
                    modLevel = &commands[i];
                else if (baseAddr >= 0x60 && baseAddr <= 0x75)
                    modAttack = &commands[i];
                else if (baseAddr >= 0x80 && baseAddr <= 0x95)
                    modSustain = &commands[i];
                else if (baseAddr >= 0xE0 && baseAddr <= 0xF5)
                    modWave = &commands[i];
            }
            else {
                if (baseAddr >= 0x20 && baseAddr <= 0x35)
                    carChar = &commands[i];
                else if (baseAddr >= 0x40 && baseAddr <= 0x55)
                    carLevel = &commands[i];
                else if (baseAddr >= 0x60 && baseAddr <= 0x75)
                    carAttack = &commands[i];
                else if (baseAddr >= 0x80 && baseAddr <= 0x95)
                    carSustain = &commands[i];
                else if (baseAddr >= 0xE0 && baseAddr <= 0xF5)
                    carWave = &commands[i];
            }
        }
        else {
            if (baseAddr >= 0xA0 && baseAddr <= 0xA8)
                freq = &commands[i];
            else if (baseAddr >= 0xB0 && baseAddr <= 0xB8) {
                if (note != NULL) {
                    int timeMs = (int)(time * 1000);
                    LOG("A decoding error occurred at " << timeMs << "ms on channel " << channel << " in range " << start << "-" << end << "\n");
                    return OPBERR_LOGGED;
                }
                note = &commands[i];
            }
            else if (baseAddr >= 0xC0 && baseAddr <= 0xC8)
                feedconn = &commands[i];
            else {
                range.push_back(cmd);
            }
        }
    }

    // combine instrument data
    int instrChanges;
    if ((instrChanges = CountInstrumentChanges(feedconn, modChar, modAttack, modSustain, modWave, carChar, carAttack, carSustain, carWave)) > 0) {
        Instrument instr = GetInstrument(context, feedconn, modChar, modAttack, modSustain, modWave, carChar, carAttack, carSustain, carWave);

        int size = Uint7Size(instr.Index) + 3;

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

        if (size < instrChanges * 2) {
            OpbData data = { 0 };
            data.WriteUint7(instr.Index);

            uint8_t channelMask = channel |
                (modLevel != NULL ? 0b00100000 : 0) |
                (carLevel != NULL ? 0b01000000 : 0) |
                (feedconn != NULL ? 0b10000000 : 0);
            data.WriteU8(channelMask);

            int mask =
                (modChar != NULL ? 0b00000001 : 0) |
                (modAttack != NULL ? 0b00000010 : 0) |
                (modSustain != NULL ? 0b00000100 : 0) |
                (modWave != NULL ? 0b00001000 : 0) |
                (carChar != NULL ? 0b00010000 : 0) |
                (carAttack != NULL ? 0b00100000 : 0) |
                (carSustain != NULL ? 0b01000000 : 0) |
                (carWave != NULL ? 0b10000000 : 0);
            data.WriteU8(mask);

            if (modLevel != NULL) data.WriteU8(modLevel->Data);
            if (carLevel != NULL) data.WriteU8(carLevel->Data);

            // instrument command is 0xD0
            int reg = OPB_CMD_SETINSTRUMENT;

            if (freq != NULL && note != NULL) {
                data.WriteU8(freq->Data);
                data.WriteU8(note->Data);

                // play command is 0xD1
                reg = OPB_CMD_PLAYINSTRUMENT;
            }

            int opbIndex = (int32_t)context.DataMap.size() + 1;
            context.DataMap[opbIndex] = data;

            Command cmd = {
                (uint16_t)(reg + (channel >= 9 ? 0x100 : 0)), // register
                0, // data
                time,
                commands[0].OrderIndex,
                opbIndex
            };

            range.push_back(cmd);
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
        data.WriteU8(freq->Data);

        int noteLevels = note->Data & 0b00111111;

        // encode modulator and carrier levels data in the note data's upper 2 (unused) bits
        if (modLevel != NULL) {
            noteLevels |= 0b01000000;
        }
        if (carLevel != NULL) {
            noteLevels |= 0b10000000;
        }

        data.WriteU8(noteLevels);

        if (modLevel != NULL) {
            data.WriteU8(modLevel->Data);
        }
        if (carLevel != NULL) {
            data.WriteU8(carLevel->Data);
        }

        int opbIndex = (int32_t)context.DataMap.size() + 1;
        context.DataMap[opbIndex] = data;

        Command cmd = {
            (uint16_t)reg, // register
            0, // data
            time,
            note->OrderIndex,
            opbIndex
        };

        range.push_back(cmd);
        freq = note = modLevel = carLevel = NULL;
    }

    if (modChar != NULL) range.push_back(*modChar);
    if (modLevel != NULL) range.push_back(*modLevel);
    if (modAttack != NULL) range.push_back(*modAttack);
    if (modSustain != NULL) range.push_back(*modSustain);
    if (modWave != NULL) range.push_back(*modWave);

    if (carChar != NULL) range.push_back(*carChar);
    if (carLevel != NULL) range.push_back(*carLevel);
    if (carAttack != NULL) range.push_back(*carAttack);
    if (carSustain != NULL) range.push_back(*carSustain);
    if (carWave != NULL) range.push_back(*carWave);

    if (feedconn != NULL) range.push_back(*feedconn);
    if (freq != NULL) range.push_back(*freq);
    if (note != NULL) range.push_back(*note);

    return 0;
}

static int ProcessTrack(Context& context, int channel, std::vector<Command>& chOut) {
    const std::vector<Command>& commands = context.Tracks[channel];

    if (commands.size() == 0) {
        return 0;
    }

    int lastIndex = 0;
    int lastOrder = commands[0].OrderIndex;
    int i = 0;

    while (i < commands.size()) {
        double time = commands[i].Time;

        int start = i;
        // sequences must be all in the same time block and in order
        // sequences are capped by a note command (write to register B0-B8 or 1B0-1B8)
        while (i < commands.size() && commands[i].Time <= time && (commands[i].OrderIndex - lastOrder) <= 1) {
            const auto& cmd = commands[i];

            lastOrder = commands[i].OrderIndex;
            i++;

            if (IsChannelNoteEvent(cmd.Addr, channel)) {
                break;
            }
        }
        int end = i;

        std::vector<Command> range;
        int ret = ProcessRange(context, channel, time, std::vector<Command>(commands.begin() + start, commands.begin() + end), range, start, end);
        if (ret) return ret;
        chOut.insert(chOut.end(), range.begin(), range.end());

        if (i < commands.size()) {
            lastOrder = commands[i].OrderIndex;
        }
    }

    return 0;
}

static int WriteChunk(Context& context, double elapsed, int start, int count) {
    uint32_t elapsedMs = (uint32_t)(elapsed * 1000);
    int loCount = 0;
    int hiCount = 0;

    for (int i = start; i < start + count; i++) {
        const Command& cmd = context.CommandStream[i];

        if ((cmd.Addr & 0x100) == 0) {
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
            const Command& cmd = context.CommandStream[i];

            if (((cmd.Addr & 0x100) == 0) == isLow) {
                uint8_t baseAddr = cmd.Addr & 0xFF;
                WRITE(&baseAddr, sizeof(uint8_t), 1, context);

                if (cmd.DataIndex) {
                    // opb command
                    OpbData data = context.DataMap[cmd.DataIndex];
                    WRITE(data.Args, sizeof(uint8_t), data.Count, context);
                }
                else {
                    // regular write
                    WRITE(&cmd.Data, sizeof(uint8_t), 1, context);
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

static int ConvertToOpb(Context& context) {
    if (context.Format < OPB_Format_Default || context.Format > OPB_Format_Raw) {
        context.Format = OPB_Format_Default;
    }

    LOG("OPB format " << context.Format << " (" << GetFormatName(context.Format) << ")\n");

    uint8_t fmt = (uint8_t)context.Format;
    WRITE(&fmt, sizeof(uint8_t), 1, context);

    if (context.Format == OPB_Format_Raw) {
        LOG("Writing RAW OPL data stream\n");
        double lastTime = 0.0;
        for (const auto& cmd : context.CommandStream) {
            uint16_t elapsed = FlipEndian16((uint16_t)((cmd.Time - lastTime) * 1000.0));
            uint16_t addr = FlipEndian16(cmd.Addr);

            WRITE(&elapsed, sizeof(uint16_t), 1, context);
            WRITE(&addr, sizeof(uint16_t), 1, context);
            WRITE(&cmd.Data, sizeof(uint8_t), 1, context);
            lastTime = cmd.Time;
        }
        return 0;
    }

    // separate command stream into tracks
    LOG("Separating OPL data stream into channels\n");
    SeparateTracks(context);

    // process each track into its own output vector
    std::vector<Command> chOut[NUM_TRACKS];
    for (int i = 0; i < NUM_TRACKS; i++) {
        LOG("Processing channel " << i << "\n");
        int ret = ProcessTrack(context, i, chOut[i]);
        if (ret) return ret;
    }

    // combine all output back into command stream
    LOG("Combining processed data into linear stream\n");
    context.CommandStream.clear();
    for (int i = 0; i < NUM_TRACKS; i++) {
        context.CommandStream.insert(context.CommandStream.end(), chOut[i].begin(), chOut[i].end());
    }

    // sort by received order
    std::sort(context.CommandStream.begin(), context.CommandStream.end(), [](Command a, Command b) {
        return a.OrderIndex < b.OrderIndex;
    });

    // write instruments table
    SEEK(context, 12, SEEK_CUR); // skip header

    LOG("Writing instrument table\n");
    for (int i = 0; i < context.Instruments.size(); i++) {
        int ret = WriteInstrument(context, context.Instruments[i]);
        if (ret) return ret;
    }

    // write chunks
    int chunks = 0;
    double lastTime = 0;
    int i = 0;

    LOG("Writing chunks\n");
    while (i < context.CommandStream.size()) {
        double chunkTime = context.CommandStream[i].Time;

        int start = i;
        while (i < context.CommandStream.size() && context.CommandStream[i].Time <= chunkTime) {
            i++;
        }
        int end = i;

        int ret = WriteChunk(context, chunkTime - lastTime, start, end - start);
        if (ret) return ret;
        chunks++;

        lastTime = chunkTime;
    }

    // write header
    LOG("Writing header\n");

    long fpos = context.Tell(context.UserData);
    if (fpos == -1L) return OPBERR_TELL_ERROR;

    uint32_t length = FlipEndian32(fpos);
    uint32_t instrCount = FlipEndian32((uint32_t)context.Instruments.size());
    uint32_t chunkCount = FlipEndian32(chunks);

    SEEK(context, 1, SEEK_SET);
    WRITE(&length, sizeof(uint32_t), 1, context);
    WRITE(&instrCount, sizeof(uint32_t), 1, context);
    WRITE(&chunkCount, sizeof(uint32_t), 1, context);

    return 0;
}

static size_t WriteToFile(const void* buffer, size_t elementSize, size_t elementCount, void* context) {
    return fwrite(buffer, elementSize, elementCount, (FILE*)context);
}

static int SeekInFile(void* context, long offset, int origin) {
    return fseek((FILE*)context, offset, origin);
}

static long TellInFile(void* context) {
    return ftell((FILE*)context);
}

int OPB_OplToFile(OPB_Format format, OPB_Command* commandStream, size_t commandCount, const char* file) {
    FILE* outFile;
    if ((outFile = fopen(file, "wb")) == NULL) {
        LOG("Couldn't open file '" << file << "' for writing\n");
        return OPBERR_LOGGED;
    }
    int ret = OPB_OplToBinary(format, commandStream, commandCount, WriteToFile, SeekInFile, TellInFile, outFile);
    if (fclose(outFile)) {
        LOG("Error while closing file '" << file << "'\n");
        return OPBERR_LOGGED;
    }
    return ret;
}

int OPB_OplToBinary(OPB_Format format, OPB_Command* commandStream, size_t commandCount, OPB_StreamWriter write, OPB_StreamSeeker seek, OPB_StreamTeller tell, void* userData) {
    Context context;

    context.Write = write;
    context.Seek = seek;
    context.Tell = tell;
    context.UserData = userData;
    context.Format = format;

    // convert stream to internal format
    for (int i = 0; i < commandCount; i++) {
        const OPB_Command& src = commandStream[i];

        Command cmd = {
            src.Addr,   // OPL register
            src.Data,   // OPL data
            src.Time,   // Time in seconds
            i,          // Stream index
            0           // Data index
        };

        context.CommandStream.push_back(cmd);
    }

    return ConvertToOpb(context);
}

static int ReadInstrument(Context& context, Instrument* instr) {
    uint8_t buffer[9];
    READ(buffer, sizeof(uint8_t), 9, context);
    *instr = {
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
        (int)context.Instruments.size() // instrument index
    };
    return 0;
}

static int ReadUint7(Context& context) {
    uint8_t b0 = 0, b1 = 0, b2 = 0, b3 = 0;

    if (context.Read(&b0, sizeof(uint8_t), 1, context.UserData) != 1) return -1;
    if (b0 >= 128) {
        b0 &= 0b01111111;
        if (context.Read(&b1, sizeof(uint8_t), 1, context.UserData) != 1) return -1;
        if (b1 >= 128) {
            b1 &= 0b01111111;
            if (context.Read(&b2, sizeof(uint8_t), 1, context.UserData) != 1) return -1;
            if (b2 >= 128) {
                b2 &= 0b01111111;
                if (context.Read(&b3, sizeof(uint8_t), 1, context.UserData) != 1) return -1;
            }
        }
    }

    return b0 | (b1 << 7) | (b2 << 14) | (b3 << 21);
}

#define DEFAULT_READBUFFER_SIZE 256

static inline int AddToBuffer(Context& context, OPB_Command* buffer, int* index, OPB_Command cmd) {
    buffer[*index] = cmd;
    (*index)++;

    if (*index >= DEFAULT_READBUFFER_SIZE) {
        SUBMIT(buffer, DEFAULT_READBUFFER_SIZE, context);
        *index = 0;
    }

    return 0;
}

#define ADD_TO_BUFFER(context, buffer, index, ...) \
    { int MACRO_CONCAT(__ret, __LINE__); \
    if ((MACRO_CONCAT(__ret, __LINE__) = AddToBuffer(context, buffer, bufferIndex, __VA_ARGS__))) return MACRO_CONCAT(__ret, __LINE__); }

static int ReadCommand(Context& context, OPB_Command* buffer, int* bufferIndex, int mask) {
    uint8_t baseAddr;
    READ(&baseAddr, sizeof(uint8_t), 1, context);

    int addr = baseAddr | mask;

    switch (baseAddr) {
        default: {
            uint8_t data;
            READ(&data, sizeof(uint8_t), 1, context);
            ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)addr, data, context.Time });
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
                LOG("Error reading OPB command: channel " << channel << " out of range\n");
                return OPBERR_LOGGED;
            }

            int mask = channelMask[1];
            bool modChr = (mask & 0b00000001) != 0;
            bool modAtk = (mask & 0b00000010) != 0;
            bool modSus = (mask & 0b00000100) != 0;
            bool modWav = (mask & 0b00001000) != 0;
            bool carChr = (mask & 0b00010000) != 0;
            bool carAtk = (mask & 0b00100000) != 0;
            bool carSus = (mask & 0b01000000) != 0;
            bool carWav = (mask & 0b10000000) != 0;

            uint8_t modLvlData = 0, carLvlData = 0;
            if (modLvl) READ(&modLvlData, sizeof(uint8_t), 1, context);
            if (carLvl) READ(&carLvlData, sizeof(uint8_t), 1, context);

            uint8_t freq = 0, note = 0;
            bool isPlay = baseAddr == OPB_CMD_PLAYINSTRUMENT;
            if (isPlay) {
                READ(&freq, sizeof(uint8_t), 1, context);
                READ(&note, sizeof(uint8_t), 1, context);
            }

            if (instrIndex < 0 || instrIndex >= context.Instruments.size()) {
                LOG("Error reading OPB command: instrument " << instrIndex << " out of range\n");
                return OPBERR_LOGGED;
            }

            Instrument& instr = context.Instruments[instrIndex];
            int conn = ChannelToOffset[channel];
            int mod = OperatorOffsets[ChannelToOp[channel]];
            int car = mod + 3;
            int playOffset = ChannelToOffset[channel];

            if (feedconn) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_FEEDCONN + conn), (uint8_t)instr.FeedConn, context.Time });
            if (modChr) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_CHARACTER + mod), (uint8_t)instr.Modulator.Characteristic, context.Time });
            if (modLvl) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_LEVELS + mod), modLvlData, context.Time });
            if (modAtk) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_ATTACK + mod), (uint8_t)instr.Modulator.AttackDecay, context.Time });
            if (modSus) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_SUSTAIN + mod), (uint8_t)instr.Modulator.SustainRelease, context.Time });
            if (modWav) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_WAVE + mod), (uint8_t)instr.Modulator.WaveSelect, context.Time });
            if (carChr) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_CHARACTER + car), (uint8_t)instr.Carrier.Characteristic, context.Time });
            if (carLvl) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_LEVELS + car), carLvlData, context.Time });
            if (carAtk) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_ATTACK + car), (uint8_t)instr.Carrier.AttackDecay, context.Time });
            if (carSus) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_SUSTAIN + car), (uint8_t)instr.Carrier.SustainRelease, context.Time });
            if (carWav) ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_WAVE + car), (uint8_t)instr.Carrier.WaveSelect, context.Time });
            if (isPlay) {
                ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_FREQUENCY + playOffset), freq, context.Time });
                ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(REG_NOTE + playOffset), note, context.Time });
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
                LOG("Error reading OPB command: channel " << channel << " out of range\n");
                return OPBERR_LOGGED;
            }

            uint8_t freqNote[2];
            READ(freqNote, sizeof(uint8_t), 2, context);

            uint8_t freq = freqNote[0];
            uint8_t note = freqNote[1];

            ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(addr - (OPB_CMD_NOTEON - REG_FREQUENCY)), freq, context.Time });
            ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)(addr - (OPB_CMD_NOTEON - REG_NOTE)), (uint8_t)(note & 0b00111111), context.Time });

            if ((note & 0b01000000) != 0) {
                // set modulator volume
                uint8_t vol;
                READ(&vol, sizeof(uint8_t), 1, context);
                int reg = REG_LEVELS + OperatorOffsets[ChannelToOp[channel]];
                ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)reg, vol, context.Time });
            }
            if ((note & 0b10000000) != 0) {
                // set carrier volume
                uint8_t vol;
                READ(&vol, sizeof(uint8_t), 1, context);
                int reg = REG_LEVELS + 3 + OperatorOffsets[ChannelToOp[channel]];
                ADD_TO_BUFFER(context, buffer, bufferIndex, { (uint16_t)reg, vol, context.Time });
            }
            break;
        }
    }

    return 0;
}

static int ReadChunk(Context& context, OPB_Command* buffer, int* bufferIndex) {
    int elapsed, loCount, hiCount;

    READ_UINT7(elapsed, context);
    READ_UINT7(loCount, context);
    READ_UINT7(hiCount, context);

    context.Time += elapsed / 1000.0;

    for (int i = 0; i < loCount; i++) {
        int ret = ReadCommand(context, buffer, bufferIndex, 0x0);
        if (ret) return ret;
    }
    for (int i = 0; i < hiCount; i++) {
        int ret = ReadCommand(context, buffer, bufferIndex, 0x100);
        if (ret) return ret;
    }

    return 0;
}

static int ReadOpbDefault(Context& context) {
    uint32_t header[3];
    READ(header, sizeof(uint32_t), 3, context);
    for (int i = 0; i < 3; i++) header[i] = FlipEndian32(header[i]);

    uint32_t instrumentCount = header[1];
    uint32_t chunkCount = header[2];

    for (uint32_t i = 0; i < instrumentCount; i++) {
        Instrument instr;
        int ret = ReadInstrument(context, &instr);
        if (ret) return ret;
        context.Instruments.push_back(instr);
    }

    OPB_Command buffer[DEFAULT_READBUFFER_SIZE];
    int bufferIndex = 0;

    for (uint32_t i = 0; i < chunkCount; i++) {
        int ret = ReadChunk(context, buffer, &bufferIndex);
        if (ret) return ret;
    }

    if (bufferIndex > 0) {
        SUBMIT(buffer, bufferIndex, context);
    }

    return 0;
}

#define RAW_READBUFFER_SIZE 256

typedef struct RawOpbEntry {
    uint16_t Elapsed;
    uint16_t Addr;
    uint8_t Data;
} RawOpbEntry;

static int ReadOpbRaw(Context& context) {
    double time = 0;
    RawOpbEntry buffer[RAW_READBUFFER_SIZE];
    OPB_Command commandStream[RAW_READBUFFER_SIZE];

    size_t itemsRead;
    while ((itemsRead = context.Read(buffer, sizeof(RawOpbEntry), RAW_READBUFFER_SIZE, context.UserData)) > 0) {
        for (int i = 0; i < itemsRead; i++) {
            time += buffer[i].Elapsed / 1000.0;
            
            OPB_Command cmd = {
                buffer[i].Addr,
                buffer[i].Data,
                time
            };
            commandStream[i] = cmd;
        }
        SUBMIT(commandStream, itemsRead, context);
    }

    return 0;
}

static int ConvertFromOpb(Context& context) {
    uint8_t fmt;
    READ(&fmt, sizeof(uint8_t), 1, context);

    switch (fmt) {
    default:
        LOG("Error reading OPB file: unknown format " << fmt << "\n");
        return OPBERR_LOGGED;
    case OPB_Format_Default:
        return ReadOpbDefault(context);
    case OPB_Format_Raw:
        return ReadOpbRaw(context);
    }
}

static size_t ReadFromFile(void* buffer, size_t elementSize, size_t elementCount, void* context) {
    return fread(buffer, elementSize, elementCount, (FILE*)context);
}

int OPB_FileToOpl(const char* file, OPB_BufferReceiver receiver, void* receiverData) {
    FILE* inFile;
    if ((inFile = fopen(file, "rb")) == NULL) {
        LOG("Couldn't open file '" << file << "' for reading\n");
        return OPBERR_LOGGED;
    }
    int ret = OPB_BinaryToOpl(ReadFromFile, inFile, receiver, receiverData);
    fclose(inFile);
    return ret;
}

int OPB_BinaryToOpl(OPB_StreamReader reader, void* readerData, OPB_BufferReceiver receiver, void* receiverData) {
    Context context;

    context.Read = reader;
    context.Submit = receiver;
    context.UserData = readerData;
    context.ReceiverData = receiverData;

    return ConvertFromOpb(context);
}