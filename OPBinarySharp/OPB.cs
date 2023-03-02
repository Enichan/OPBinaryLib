// MIT License
// 
// Copyright (c) 2023 Eniko Fox/Emma Maassen
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace OPBinarySharp {
    public delegate void LogHandler(string s);
    public delegate void BufferReceiver(OPBCommand[] commandStream, int commandCount, object context);

    public static class OPB {
        public static LogHandler LogHandler;

        #region Public methods
#if !OPB_READONLY
        public static void OplToBinary(OPBFormat format, IList<OPBCommand> commandStream, Stream outStream) {
            var context = new WriteContext();

            context.Stream = outStream;
            context.Format = format;

            // convert stream to internal format
            int orderIndex = 0;
            for (int i = 0; i < commandStream.Count; i++) {
                var source = commandStream[i];

                if (IsSpecialCommand(source.Addr)) {
                    Log(string.Format("Illegal register 0x{0} with value 0x{1} in command stream, ignored", source.Addr.ToString("X3"), source.Data.ToString("X2")));
                }
                else {
                    var cmd = new Command() {
                        Addr = source.Addr,                // OPL register
                        Data = source.Data,                // OPL data
                        Time = source.Time.TotalSeconds,   // Time in seconds
                        OrderIndex = orderIndex++,         // Stream index
                        DataIndex = 0                      // Data index
                    };

                    context.CommandStream.Add(cmd);
                }
            }

            ConvertToOpb(context);
        }

        public static void OplToFile(OPBFormat format, IList<OPBCommand> commandStream, string file) {
            using (var stream = File.Open(file, FileMode.Create, FileAccess.Write)) {
                OplToBinary(format, commandStream, stream);
            }
        }
#endif

        public static void BinaryToOpl(Stream sourceStream, BufferReceiver receiver, object receiverData) {
            var context = new ReadContext();

            context.Submit = receiver;
            context.Stream = sourceStream;
            context.ReceiverData = receiverData;
            context.Instruments = new List<Instrument>();

            ConvertFromOpb(context);
        }

        public static List<OPBCommand> BinaryToOpl(Stream sourceStream) {
            var list = new List<OPBCommand>();
            BinaryToOpl(sourceStream, ListReceiver, list);
            return list;
        }

        public static void FileToOpl(string file, BufferReceiver receiver, object receiverData) {
            using (var stream = File.OpenRead(file)) {
                BinaryToOpl(stream, receiver, receiverData);
            }
        }

        public static List<OPBCommand> FileToOpl(string file) {
            var list = new List<OPBCommand>();
            FileToOpl(file, ListReceiver, list);
            return list;
        }
#endregion

        private const byte opbCmdSetInstrument = 0xD0;
        private const byte opbCmdPlayInstrument = 0xD1;
        private const byte opbCmdNoteOn = 0xD7;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static bool IsSpecialCommand(int addr) {
            addr = addr & 0xFF;
            return addr >= 0xD0 && addr <= 0xDF;
        }

        private const byte regFeedconn  = 0xC0;
        private const byte regCharacter = 0x20;
        private const byte regLevels    = 0x40;
        private const byte regAttack    = 0x60;
        private const byte regSustain   = 0x80;
        private const byte regWave      = 0xE0;
        private const byte regFrequency = 0xA0;
        private const byte regNote      = 0xB0;

        private const int numChannels = 18;
        private const int numTracks = numChannels + 1;
        private const int numOperators = 36;

        private static int[] operatorOffsets = new int[numOperators] {
            0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
            0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x108, 0x109, 0x10A, 0x10B, 0x10C, 0x10D, 0x110, 0x111, 0x112, 0x113, 0x114, 0x115,
        };

        private static int[] channelToOp = new int[numChannels] {
            0, 1, 2, 6, 7, 8, 12, 13, 14, 18, 19, 20, 24, 25, 26, 30, 31, 32,
        };

        private static int[] channelToOffset = new int[numChannels] {
            0, 1, 2, 3, 4, 5, 6, 7, 8,
            0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108,
        };

        private const int opbHeaderSize = 7;
        // OPBin1\0
        private static byte[] opbHeader = new byte[opbHeaderSize] { (byte)'O', (byte)'P', (byte)'B', (byte)'i', (byte)'n', (byte)'1', 0 };

        private static void ListReceiver(OPBCommand[] commandStream, int commandCount, object context) {
            var list = (List<OPBCommand>)context;
            if (commandCount == commandStream.Length) {
                list.AddRange(commandStream);
            }
            else {
                for (int i = 0; i < commandCount; i++) {
                    list.Add(commandStream[i]);
                }
            }
        }

#pragma warning disable 0649
        struct Command {
            public ushort Addr;
            public byte Data;
            public double Time;
            public int OrderIndex;
            public int DataIndex;

            public override string ToString() {
                return string.Format("{2} at {3}: 0x{0} 0x{1} ({4})", Addr.ToString("X3"), Data.ToString("X2"), OrderIndex, Time, DataIndex);
            }
        }
#pragma warning restore 0649

        struct OpbData {
            public int Count;
            public byte Arg0,  Arg1,  Arg2,  Arg3;
            public byte Arg4,  Arg5,  Arg6,  Arg7;
            public byte Arg8,  Arg9,  Arg10, Arg11;
            public byte Arg12, Arg13, Arg14, Arg15;

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static void WriteUint7(ref OpbData data, uint value) {
                if (value >= 2097152) {
                    WriteU8(ref data, (value & 0b01111111) | 0b10000000);
                    WriteU8(ref data, ((value & 0b011111110000000) >> 7) | 0b10000000);
                    WriteU8(ref data, ((value & 0b0111111100000000000000) >> 14) | 0b10000000);
                    WriteU8(ref data, (value & 0b11111111000000000000000000000) >> 21);
                }
                else if (value >= 16384) {
                    WriteU8(ref data, (value & 0b01111111) | 0b10000000);
                    WriteU8(ref data, ((value & 0b011111110000000) >> 7) | 0b10000000);
                    WriteU8(ref data, (value & 0b0111111100000000000000) >> 14);
                }
                else if (value >= 128) {
                    WriteU8(ref data, (value & 0b01111111) | 0b10000000);
                    WriteU8(ref data, (value & 0b011111110000000) >> 7);
                }
                else {
                    WriteU8(ref data, value & 0b01111111);
                }
            }

            [MethodImpl(MethodImplOptions.AggressiveInlining)]
            public static void WriteU8(ref OpbData data, uint value) {
                switch (data.Count) {
                    default:
                        throw new OPBException("Data overflow in OpbData instance");
                    case 0: data.Arg0 = (byte)value; data.Count++; break;
                    case 1: data.Arg1 = (byte)value; data.Count++; break;
                    case 2: data.Arg2 = (byte)value; data.Count++; break;
                    case 3: data.Arg3 = (byte)value; data.Count++; break;
                    case 4: data.Arg4 = (byte)value; data.Count++; break;
                    case 5: data.Arg5 = (byte)value; data.Count++; break;
                    case 6: data.Arg6 = (byte)value; data.Count++; break;
                    case 7: data.Arg7 = (byte)value; data.Count++; break;
                    case 8: data.Arg8 = (byte)value; data.Count++; break;
                    case 9: data.Arg9 = (byte)value; data.Count++; break;
                    case 10: data.Arg10 = (byte)value; data.Count++; break;
                    case 11: data.Arg11 = (byte)value; data.Count++; break;
                    case 12: data.Arg12 = (byte)value; data.Count++; break;
                    case 13: data.Arg13 = (byte)value; data.Count++; break;
                    case 14: data.Arg14 = (byte)value; data.Count++; break;
                    case 15: data.Arg15 = (byte)value; data.Count++; break;
                }
            }
        }

        struct Operator {
            public short Characteristic;
            public short AttackDecay;
            public short SustainRelease;
            public short WaveSelect;
        }

        struct Instrument {
            public short FeedConn;
            public Operator Modulator;
            public Operator Carrier;
            public int Index;

            public bool CanCombine(out Instrument combined, in Command? feedconn,
                in Command? modChar, in Command? modAttack, in Command? modSustain, in Command? modWave,
                in Command? carChar, in Command? carAttack, in Command? carSustain, in Command? carWave) {
                if ((!feedconn.HasValue || FeedConn == feedconn.Value.Data || FeedConn < 0) &&
                    (!modChar.HasValue || Modulator.Characteristic == modChar.Value.Data || Modulator.Characteristic < 0) &&
                    (!modAttack.HasValue || Modulator.AttackDecay == modAttack.Value.Data || Modulator.AttackDecay < 0) &&
                    (!modSustain.HasValue || Modulator.SustainRelease == modSustain.Value.Data || Modulator.SustainRelease < 0) &&
                    (!modWave.HasValue || Modulator.WaveSelect == modWave.Value.Data || Modulator.WaveSelect < 0) &&
                    (!carChar.HasValue || Carrier.Characteristic == carChar.Value.Data || Carrier.Characteristic < 0) &&
                    (!carAttack.HasValue || Carrier.AttackDecay == carAttack.Value.Data || Carrier.AttackDecay < 0) &&
                    (!carSustain.HasValue || Carrier.SustainRelease == carSustain.Value.Data || Carrier.SustainRelease < 0) &&
                    (!carWave.HasValue || Carrier.WaveSelect == carWave.Value.Data || Carrier.WaveSelect < 0)) {
                    combined = this;
                    combined.FeedConn = feedconn.HasValue ? feedconn.Value.Data : FeedConn;
                    combined.Modulator.Characteristic = modChar.HasValue ? modChar.Value.Data : Modulator.Characteristic;
                    combined.Modulator.AttackDecay = modAttack.HasValue ? modAttack.Value.Data : Modulator.AttackDecay;
                    combined.Modulator.SustainRelease = modSustain.HasValue ? modSustain.Value.Data : Modulator.SustainRelease;
                    combined.Modulator.WaveSelect = modWave.HasValue ? modWave.Value.Data : Modulator.WaveSelect;
                    combined.Carrier.Characteristic = carChar.HasValue ? carChar.Value.Data : Carrier.Characteristic;
                    combined.Carrier.AttackDecay = carAttack.HasValue ? carAttack.Value.Data : Carrier.AttackDecay;
                    combined.Carrier.SustainRelease = carSustain.HasValue ? carSustain.Value.Data : Carrier.SustainRelease;
                    combined.Carrier.WaveSelect = carWave.HasValue ? carWave.Value.Data : Carrier.WaveSelect;
                    return true;
                }
                combined = default(Instrument);
                return false;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static string Log(string s) {
            if (LogHandler == null) {
                return s;
            }
            LogHandler(s + Environment.NewLine);
            return s;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static string TSI(int value) {
            return value.ToString(CultureInfo.InvariantCulture);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int Uint7Size(uint value) {
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

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int RegisterOffsetToChannel(uint offset) {
            uint baseoff = offset & 0xFF;
            int chunk = (int)(baseoff / 8);
            int suboff = (int)(baseoff % 8);

            if (chunk >= 3 || suboff >= 6) {
                return -1;
            }
            return chunk * 3 + (suboff % 3) + ((offset & 0x100) != 0 ? numChannels / 2 : 0);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int RegisterOffsetToOpIndex(uint offset) {
            uint baseoff = offset & 0xFF;
            uint suboff = baseoff % 8;
            if (suboff >= 6) {
                return -1;
            }
            return suboff >= 3 ? 1 : 0;
        }

#if !OPB_READONLY
        class WriteContext {
            public Stream Stream;
            public List<Instrument> Instruments;
            public List<Command>[] Tracks;
            public List<Command> CommandStream;
            public OPBFormat Format;
            public List<OpbData> DataMap;
            public List<Command> Range;

            public WriteContext() {
                CommandStream = new List<Command>(0);
                Instruments = new List<Instrument>(0);
                DataMap = new List<OpbData>(0);

                Tracks = new List<Command>[numTracks];
                for (int i = 0; i < Tracks.Length; i++) {
                    Tracks[i] = new List<Command>(0);
                }
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void WriteUint7(WriteContext context, in uint value) {
            if (value >= 2097152) {
                context.Stream.WriteByte((byte)((value & 0b01111111) | 0b10000000));
                context.Stream.WriteByte((byte)(((value & 0b011111110000000) >> 7) | 0b10000000));
                context.Stream.WriteByte((byte)(((value & 0b0111111100000000000000) >> 14) | 0b10000000));
                context.Stream.WriteByte((byte)((value & 0b11111111000000000000000000000) >> 21));
            }
            else if (value >= 16384) {
                context.Stream.WriteByte((byte)((value & 0b01111111) | 0b10000000));
                context.Stream.WriteByte((byte)(((value & 0b011111110000000) >> 7) | 0b10000000));
                context.Stream.WriteByte((byte)((value & 0b0111111100000000000000) >> 14));
            }
            else if (value >= 128) {
                context.Stream.WriteByte((byte)((value & 0b01111111) | 0b10000000));
                context.Stream.WriteByte((byte)((value & 0b011111110000000) >> 7));
            }
            else {
                context.Stream.WriteByte((byte)(value & 0b01111111));
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in byte value) {
            context.Stream.WriteByte(value);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in OpbData value) {
            if (value.Count > 0)  context.Stream.WriteByte(value.Arg0);
            if (value.Count > 1)  context.Stream.WriteByte(value.Arg1);
            if (value.Count > 2)  context.Stream.WriteByte(value.Arg2);
            if (value.Count > 3)  context.Stream.WriteByte(value.Arg3);
            if (value.Count > 4)  context.Stream.WriteByte(value.Arg4);
            if (value.Count > 5)  context.Stream.WriteByte(value.Arg5);
            if (value.Count > 6)  context.Stream.WriteByte(value.Arg6);
            if (value.Count > 7)  context.Stream.WriteByte(value.Arg7);
            if (value.Count > 8)  context.Stream.WriteByte(value.Arg8);
            if (value.Count > 9)  context.Stream.WriteByte(value.Arg9);
            if (value.Count > 10) context.Stream.WriteByte(value.Arg10);
            if (value.Count > 11) context.Stream.WriteByte(value.Arg11);
            if (value.Count > 12) context.Stream.WriteByte(value.Arg12);
            if (value.Count > 13) context.Stream.WriteByte(value.Arg13);
            if (value.Count > 14) context.Stream.WriteByte(value.Arg14);
            if (value.Count > 15) context.Stream.WriteByte(value.Arg15);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in ushort val) {
            context.Stream.WriteByte((byte)(val >> 8));
            context.Stream.WriteByte((byte)(val & 0xFF));
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in short val) {
            Write(context, (ushort)val);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in uint val) {
            context.Stream.WriteByte((byte)((val >> 24) & 0xFF));
            context.Stream.WriteByte((byte)((val >> 16) & 0xFF));
            context.Stream.WriteByte((byte)((val >> 8) & 0xFF));
            context.Stream.WriteByte((byte)(val & 0xFF));
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void Write(WriteContext context, in int val) {
            Write(context, (uint)val);
        }

        private static Instrument GetInstrument(WriteContext context, in Command? feedconn,
            in Command? modChar, in Command? modAttack, in Command? modSustain, in Command? modWave,
            in Command? carChar, in Command? carAttack, in Command? carSustain, in Command? carWave) {

            // find a matching instrument
            for (int i = 0; i < context.Instruments.Count; i++) {
                Instrument combined;
                if (context.Instruments[i].CanCombine(out combined, in feedconn, in modChar, in modAttack, in modSustain, in modWave, in carChar, in carAttack, in carSustain, in carWave)) {
                    context.Instruments[i] = combined;
                    return context.Instruments[i];
                }
            }

            // no instrument found, create and store new instrument
            var instr = new Instrument {
                FeedConn = (short)(feedconn.HasValue ? feedconn.Value.Data : -1),
                Modulator = new Operator {
                    Characteristic = (short)(modChar.HasValue ? modChar.Value.Data : -1),
                    AttackDecay = (short)(modAttack.HasValue ? modAttack.Value.Data : -1),
                    SustainRelease = (short)(modSustain.HasValue ? modSustain.Value.Data : -1),
                    WaveSelect = (short)(modWave.HasValue ? modWave.Value.Data : -1),
                },
                Carrier = new Operator {
                    Characteristic = (short)(carChar.HasValue ? carChar.Value.Data : -1),
                    AttackDecay = (short)(carAttack.HasValue ? carAttack.Value.Data : -1),
                    SustainRelease = (short)(carSustain.HasValue ? carSustain.Value.Data : -1),
                    WaveSelect = (short)(carWave.HasValue ? carWave.Value.Data : -1),
                },
                Index = context.Instruments.Count
            };
            context.Instruments.Add(instr);
            return instr;
        }

        private static void WriteInstrument(WriteContext context, in Instrument instr) {
            byte feedConn = (byte)(instr.FeedConn >= 0 ? instr.FeedConn : 0);
            byte modChr = (byte)(instr.Modulator.Characteristic >= 0 ? instr.Modulator.Characteristic : 0);
            byte modAtk = (byte)(instr.Modulator.AttackDecay >= 0 ? instr.Modulator.AttackDecay : 0);
            byte modSus = (byte)(instr.Modulator.SustainRelease >= 0 ? instr.Modulator.SustainRelease : 0);
            byte modWav = (byte)(instr.Modulator.WaveSelect >= 0 ? instr.Modulator.WaveSelect : 0);
            byte carChr = (byte)(instr.Carrier.Characteristic >= 0 ? instr.Carrier.Characteristic : 0);
            byte carAtk = (byte)(instr.Carrier.AttackDecay >= 0 ? instr.Carrier.AttackDecay : 0);
            byte carSus = (byte)(instr.Carrier.SustainRelease >= 0 ? instr.Carrier.SustainRelease : 0);
            byte carWav = (byte)(instr.Carrier.WaveSelect >= 0 ? instr.Carrier.WaveSelect : 0);

            context.Stream.WriteByte(feedConn);
            context.Stream.WriteByte(modChr);
            context.Stream.WriteByte(modAtk);
            context.Stream.WriteByte(modSus);
            context.Stream.WriteByte(modWav);
            context.Stream.WriteByte(carChr);
            context.Stream.WriteByte(carAtk);
            context.Stream.WriteByte(carSus);
            context.Stream.WriteByte(carWav);
        }

        // returns channel for note event or -1 if not a note event
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int IsNoteEvent(int addr) {
            int baseAddr = addr & 0xFF;
            if (baseAddr >= 0xB0 && baseAddr <= 0xB8) {
                return (baseAddr - 0xB0) * ((addr & 0x100) == 0 ? 1 : 2);
            }
            else if (baseAddr >= opbCmdNoteOn && baseAddr < opbCmdNoteOn + numChannels / 2) {
                return (baseAddr - opbCmdNoteOn) * ((addr & 0x100) == 0 ? 1 : 2);
            }
            return -1;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static bool IsChannelNoteEvent(int addr, int channel) {
            return
                (addr == 0xB0 + (channel % 9) + (channel >= 9 ? 0x100 : 0)) ||
                (addr == opbCmdNoteOn + (channel % 9) + (channel >= 9 ? 0x100 : 0));
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int ChannelFromRegister(int reg) {
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
                if ((ch = RegisterOffsetToChannel((uint)offset)) >= 0) {
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
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int RegisterToOpIndex(int reg) {
            int baseReg = reg & 0xFF;
            if ((baseReg >= 0x20 && baseReg <= 0x95) || (baseReg >= 0xE0 && baseReg <= 0xF5)) {
                int offset = baseReg % 0x20;
                if (offset < 0 || offset >= 0x16) {
                    return -1;
                }
                int op;
                if ((op = RegisterOffsetToOpIndex((uint)offset)) >= 0) {
                    return op;
                }
            }
            return -1;
        }

        private static void SeparateTracks(WriteContext context) {
            for (int i = 0; i < context.CommandStream.Count; i++) {
                Command cmd = context.CommandStream[i];

                int channel = ChannelFromRegister(cmd.Addr);
                if (channel < 0) channel = numTracks - 1;

                context.Tracks[channel].Add(cmd);
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int CountInstrumentChanges(in Command? feedconn,
            in Command? modChar, in Command? modAttack, in Command? modSustain, in Command? modWave,
            in Command? carChar, in Command? carAttack, in Command? carSustain, in Command? carWave) {
            int count = 0;
            if (feedconn.HasValue) count++;
            if (modChar.HasValue) count++;
            if (modAttack.HasValue) count++;
            if (modSustain.HasValue) count++;
            if (modWave.HasValue) count++;
            if (carChar.HasValue) count++;
            if (carAttack.HasValue) count++;
            if (carSustain.HasValue) count++;
            if (carWave.HasValue) count++;
            return count;
        }

        private static void ProcessRange(WriteContext context, int channel, double time, List<Command> commands, int start, int end, List<Command> range) {
            var _debug_start = start;
            var _debug_end = end;

            for (int i = start; i < end; i++) {
                if (commands[i].Time != time) {
                    int timeMs = (int)(time * 1000);
                    var error = Log(string.Format("A timing error occurred at {0} ms on channel {1} in range {2}-{3}", TSI(timeMs), TSI(channel), TSI(_debug_start), TSI(_debug_end)));
                    throw new OPBException(error);
                }
            }

            Command? modChar = null, modLevel = null, modAttack = null, modSustain = null, modWave = null;
            Command? carChar = null, carLevel = null, carAttack = null, carSustain = null, carWave = null;
            Command? freq = null, note = null, feedconn = null;

            for (int i = start; i < end; i++) {
                var cmd = commands[i];

                int baseAddr = cmd.Addr & 0xFF;
                int op;


                if ((op = RegisterToOpIndex(cmd.Addr)) > -1) {
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
                        if (note.HasValue) {
                            int timeMs = (int)(time * 1000);
                            var error = Log(string.Format("A decoding error occurred at {0} ms on channel {1} in range {2}-{3}", TSI(timeMs), TSI(channel), TSI(_debug_start), TSI(_debug_end)));
                            throw new OPBException(error);
                        }
                        note = cmd;
                    }
                    else if (baseAddr >= 0xC0 && baseAddr <= 0xC8)
                        feedconn = cmd;
                    else {
                        range.Add(cmd);
                    }
                }
            }

            // combine instrument data
            int instrChanges;
            if ((instrChanges = CountInstrumentChanges(in feedconn, in modChar, in modAttack, in modSustain, in modWave, in carChar, in carAttack, in carSustain, in carWave)) > 0) {
                Instrument instr = GetInstrument(context, in feedconn, in modChar, in modAttack, in modSustain, in modWave, in carChar, in carAttack, in carSustain, in carWave);

                int size = Uint7Size((uint)instr.Index) + 3;

                if (modLevel.HasValue) {
                    size++;
                    instrChanges++;
                }
                if (carLevel.HasValue) {
                    size++;
                    instrChanges++;
                }

                // combine with frequency and note command if present
                if (freq.HasValue && note.HasValue) {
                    size += 2;
                    instrChanges += 2;
                }

                if (size < instrChanges * 2) {
                    OpbData data = new OpbData();
                    OpbData.WriteUint7(ref data, (uint)instr.Index);

                    byte channelMask = (byte)(channel |
                        (modLevel.HasValue ? 0b00100000 : 0) |
                        (carLevel.HasValue ? 0b01000000 : 0) |
                        (feedconn.HasValue ? 0b10000000 : 0));
                    OpbData.WriteU8(ref data, channelMask);

                    int mask =
                        (modChar.HasValue ? 0b00000001 : 0) |
                        (modAttack.HasValue ? 0b00000010 : 0) |
                        (modSustain.HasValue ? 0b00000100 : 0) |
                        (modWave.HasValue ? 0b00001000 : 0) |
                        (carChar.HasValue ? 0b00010000 : 0) |
                        (carAttack.HasValue ? 0b00100000 : 0) |
                        (carSustain.HasValue ? 0b01000000 : 0) |
                        (carWave.HasValue ? 0b10000000 : 0);
                    OpbData.WriteU8(ref data, (byte)mask);

                    // instrument command is 0xD0
                    int reg = opbCmdSetInstrument;

                    if (freq.HasValue && note.HasValue) {
                        OpbData.WriteU8(ref data, freq.Value.Data);
                        OpbData.WriteU8(ref data, note.Value.Data);

                        // play command is 0xD1
                        reg = opbCmdPlayInstrument;
                    }

                    if (modLevel.HasValue) OpbData.WriteU8(ref data, modLevel.Value.Data);
                    if (carLevel.HasValue) OpbData.WriteU8(ref data, carLevel.Value.Data);

                    int opbIndex = context.DataMap.Count + 1;
                    context.DataMap.Add(data);

                    Command cmd = new Command() {
                        Addr = (ushort)(reg + (channel >= 9 ? 0x100 : 0)), // register
                        Data = 0,
                        Time = time,
                        OrderIndex = commands[start].OrderIndex,
                        DataIndex = opbIndex
                    };

                    range.Add(cmd);
                    feedconn = modChar = modLevel = modAttack = modSustain = modWave = carChar = carLevel = carAttack = carSustain = carWave = null;

                    if (freq.HasValue && note.HasValue) {
                        freq = note = null;
                    }
                }
            }

            // combine frequency/note and modulator and carrier level data
            if (freq.HasValue && note.HasValue) {
                // note on command is 0xD7 through 0xDF (and 0x1D7 through 0x1DF for channels 10-18)
                int reg = opbCmdNoteOn + (channel % 9) + (channel >= 9 ? 0x100 : 0);

                OpbData data = new OpbData();
                OpbData.WriteU8(ref data, freq.Value.Data);

                int noteLevels = note.Value.Data & 0b00111111;

                // encode modulator and carrier levels data in the note data's upper 2 (unused) bits
                if (modLevel.HasValue) {
                    noteLevels |= 0b01000000;
                }
                if (carLevel.HasValue) {
                    noteLevels |= 0b10000000;
                }

                OpbData.WriteU8(ref data, (uint)noteLevels);

                if (modLevel.HasValue) {
                    OpbData.WriteU8(ref data, modLevel.Value.Data);
                }
                if (carLevel.HasValue) {
                    OpbData.WriteU8(ref data, carLevel.Value.Data);
                }

                int opbIndex = context.DataMap.Count + 1;
                context.DataMap.Add(data);

                Command cmd = new Command() {
                    Addr = (ushort)reg,
                    Data = 0,
                    Time = time,
                    OrderIndex = note.Value.OrderIndex,
                    DataIndex = opbIndex
                };

                range.Add(cmd);
                freq = note = modLevel = carLevel = null;
            }

            if (modChar.HasValue) range.Add(modChar.Value);
            if (modLevel.HasValue) range.Add(modLevel.Value);
            if (modAttack.HasValue) range.Add(modAttack.Value);
            if (modSustain.HasValue) range.Add(modSustain.Value);
            if (modWave.HasValue) range.Add(modWave.Value);

            if (carChar.HasValue) range.Add(carChar.Value);
            if (carLevel.HasValue) range.Add(carLevel.Value);
            if (carAttack.HasValue) range.Add(carAttack.Value);
            if (carSustain.HasValue) range.Add(carSustain.Value);
            if (carWave.HasValue) range.Add(carWave.Value);

            if (feedconn.HasValue) range.Add(feedconn.Value);
            if (freq.HasValue) range.Add(freq.Value);
            if (note.HasValue) range.Add(note.Value);
        }

        private static void ProcessTrack(WriteContext context, int channel, List<Command> chOut) {
            var commands = context.Tracks[channel];

            if (commands.Count == 0) {
                return;
            }

            int lastOrder = commands[0].OrderIndex;
            int i = 0;

            while (i < commands.Count) {
                double time = commands[i].Time;

                int start = i;
                // sequences must be all in the same time block and in order
                // sequences are capped by a note command (write to register B0-B8 or 1B0-1B8)
                while (i < commands.Count && commands[i].Time <= time && (commands[i].OrderIndex - lastOrder) <= 1) {
                    var cmd = commands[i];

                    lastOrder = cmd.OrderIndex;
                    i++;

                    if (IsChannelNoteEvent(cmd.Addr, channel)) {
                        break;
                    }
                }
                int end = i;

                var range = context.Range ?? (context.Range = new List<Command>());
                ProcessRange(context, channel, time, commands, start, end, range);

                chOut.AddRange(range);
                range.Clear();

                if (i < commands.Count) {
                    lastOrder = commands[i].OrderIndex;
                }
            }
        }

        private static void WriteChunk(WriteContext context, double elapsed, int start, int count) {
            uint elapsedMs = (uint)((elapsed * 1000) + 0.5);
            int loCount = 0;
            int hiCount = 0;

            for (int i = start; i < start + count; i++) {
                if ((context.CommandStream[i].Addr & 0x100) == 0) {
                    loCount++;
                }
                else {
                    hiCount++;
                }
            }

            // write header
            WriteUint7(context, elapsedMs);
            WriteUint7(context, (uint)loCount);
            WriteUint7(context, (uint)hiCount);

            // write low and high register writes
            bool isLow = true;
            while (true) {
                for (int i = start; i < start + count; i++) {
                    Command cmd = context.CommandStream[i];

                    if (((cmd.Addr & 0x100) == 0) == isLow) {
                        byte baseAddr = (byte)(cmd.Addr & 0xFF);
                        Write(context, baseAddr);

                        if (cmd.DataIndex != 0) {
                            if (!IsSpecialCommand(baseAddr)) {
                                throw new OPBException("Unexpected write error. Command had DataIndex but was not an OPB command");
                            }

                            // opb command
                            OpbData data = context.DataMap[cmd.DataIndex - 1];
                            Write(context, data);
                        }
                        else {
                            if (IsSpecialCommand(baseAddr)) {
                                throw new OPBException("Unexpected write error. Command was an OPB command but had no DataIndex");
                            }

                            // regular write
                            Write(context, cmd.Data);
                        }
                    }
                }

                if (!isLow) {
                    break;
                }

                isLow = !isLow;
            }
        }

        private static void ConvertToOpb(WriteContext context) {
            if (context.Format < OPBFormat.Default || context.Format > OPBFormat.Raw) {
                context.Format = OPBFormat.Default;
            }

            context.Stream.Write(opbHeader, 0, opbHeader.Length);

            Log(string.Format("OPB format {0} ({1})", TSI((int)context.Format), context.Format));

            byte fmt = (byte)context.Format;
            Write(context, fmt);

            if (context.Format == OPBFormat.Raw) {
                Log("Writing raw OPL data stream");

                double lastTime = 0.0;
                for (int i = 0; i < context.CommandStream.Count; i++) {
                    Command cmd = context.CommandStream[i];

                    ushort elapsed = (ushort)((cmd.Time - lastTime) * 1000.0);
                    ushort addr = cmd.Addr;

                    Write(context, elapsed);
                    Write(context, addr);
                    Write(context, cmd.Data);

                    lastTime = cmd.Time;
                }
                return;
            }

            // separate command stream into tracks
            Log("Separating OPL data stream into channels");
            SeparateTracks(context);

            // process each track into its own output vector
            List<Command>[] chOut = new List<Command>[numTracks];

            for (int i = 0; i < numTracks; i++) {
                Log(string.Format("Processing channel {0}", TSI(i)));
                chOut[i] = new List<Command>();

                ProcessTrack(context, i, chOut[i]);
            }

            // combine all output back into command stream
            Log("Combining processed data into linear stream");
            context.CommandStream.Clear();
            for (int i = 0; i < numTracks; i++) {
                context.CommandStream.AddRange(chOut[i]);
            }

            // sort by received order
            context.CommandStream.Sort((a, b) => a.OrderIndex - b.OrderIndex);

            // write instruments table
            context.Stream.Seek(12, SeekOrigin.Current); // skip header

            Log("Writing instrument table");
            for (int i = 0; i < context.Instruments.Count; i++) {
                WriteInstrument(context, context.Instruments[i]);
            }

            // write chunks
            {
                int chunks = 0;
                double lastTime = 0;
                int i = 0;

                Log("Writing chunks");
                while (i < context.CommandStream.Count) {
                    double chunkTime = context.CommandStream[i].Time;

                    int start = i;
                    while (i < context.CommandStream.Count && context.CommandStream[i].Time <= chunkTime) {
                        i++;
                    }
                    int end = i;

                    WriteChunk(context, chunkTime - lastTime, start, end - start);
                    chunks++;

                    lastTime = chunkTime;
                }

                // write header
                Log("Writing header");

                long fpos = context.Stream.Position;

                uint length = (uint)fpos;
                uint instrCount = (uint)context.Instruments.Count;
                uint chunkCount = (uint)chunks;

                context.Stream.Seek(opbHeaderSize + 1, SeekOrigin.Begin);
                Write(context, length);
                Write(context, instrCount);
                Write(context, chunkCount);
            }
        }
#endif

        class ReadContext {
            public Stream Stream;
            public BufferReceiver Submit;
            public List<Instrument> Instruments;
            public TimeSpan Time;
            public object ReceiverData;

            public ReadContext() {
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static byte ReadByte(ReadContext context) {
            var val = context.Stream.ReadByte();
            if (val < 0) {
                throw new OPBException("Unexpected end of stream, expected byte");
            }
            return (byte)val;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static uint ReadUint32(ReadContext context) {
            var b0 = (uint)ReadByte(context);
            var b1 = (uint)ReadByte(context);
            var b2 = (uint)ReadByte(context);
            var b3 = (uint)ReadByte(context);
            return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static Instrument ReadInstrument(ReadContext context) {
            var feedconn = ReadByte(context);
            var modChar = ReadByte(context);
            var modAttack = ReadByte(context);
            var modSustain = ReadByte(context);
            var modWave = ReadByte(context);
            var carChar = ReadByte(context);
            var carAttack = ReadByte(context);
            var carSustain = ReadByte(context);
            var carWave = ReadByte(context);
            return new Instrument() {
                FeedConn = feedconn,
                Modulator = new Operator() {
                    Characteristic = modChar,
                    AttackDecay = modAttack,
                    SustainRelease = modSustain,
                    WaveSelect = modWave,
                },
                Carrier = new Operator() {
                    Characteristic = carChar,
                    AttackDecay = carAttack,
                    SustainRelease = carSustain,
                    WaveSelect = carWave,
                },
                Index = context.Instruments.Count
            };
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static int ReadUint7(ReadContext context) {
            byte b0 = 0, b1 = 0, b2 = 0, b3 = 0;

            b0 = ReadByte(context);
            if (b0 >= 128) {
                b0 &= 0b01111111;
                b1 = ReadByte(context);
                if (b1 >= 128) {
                    b1 &= 0b01111111;
                    b2 = ReadByte(context);
                    if (b2 >= 128) {
                        b2 &= 0b01111111;
                        b3 = ReadByte(context);
                    }
                }
            }

            return b0 | (b1 << 7) | (b2 << 14) | (b3 << 21);
        }

        private const int defaultReadBufferSize = 256;

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void AddToBuffer(ReadContext context, OPBCommand[] buffer, ref int index, OPBCommand cmd) {
            buffer[index] = cmd;
            index++;

            if (index >= defaultReadBufferSize) {
                context.Submit(buffer, defaultReadBufferSize, context.ReceiverData);
                index = 0;
            }
        }

        private static void ReadCommand(ReadContext context, OPBCommand[] buffer, ref int bufferIndex, int mask) {
            byte baseAddr;
            baseAddr = ReadByte(context);

            int addr = baseAddr | mask;

            switch (baseAddr) {
                default: {
                        byte data;
                        data = ReadByte(context);
                        AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)addr, data, context.Time));
                        break;
                    }

                case opbCmdPlayInstrument:
                case opbCmdSetInstrument: {
                        int instrIndex;
                        instrIndex = ReadUint7(context);

                        byte channelMask0, channelMask1;
                        channelMask0 = ReadByte(context);
                        channelMask1 = ReadByte(context);

                        int channel = channelMask0;
                        bool modLvl = (channel & 0b00100000) != 0;
                        bool carLvl = (channel & 0b01000000) != 0;
                        bool feedconn = (channel & 0b10000000) != 0;
                        channel &= 0b00011111;

                        if (channel < 0 || channel >= numChannels) {
                            var error = Log(string.Format("Error reading OPB command: channel {0} out of range", TSI(channel)));
                            throw new OPBException(error);
                        }

                        int chmask = channelMask1;
                        bool modChr = (chmask & 0b00000001) != 0;
                        bool modAtk = (chmask & 0b00000010) != 0;
                        bool modSus = (chmask & 0b00000100) != 0;
                        bool modWav = (chmask & 0b00001000) != 0;
                        bool carChr = (chmask & 0b00010000) != 0;
                        bool carAtk = (chmask & 0b00100000) != 0;
                        bool carSus = (chmask & 0b01000000) != 0;
                        bool carWav = (chmask & 0b10000000) != 0;

                        byte freq = 0, note = 0;
                        bool isPlay = baseAddr == opbCmdPlayInstrument;
                        if (isPlay) {
                            freq = ReadByte(context);
                            note = ReadByte(context);
                        }

                        byte modLvlData = 0, carLvlData = 0;
                        if (modLvl) modLvlData = ReadByte(context);
                        if (carLvl) carLvlData = ReadByte(context);

                        if (instrIndex < 0 || instrIndex >= context.Instruments.Count) {
                            var error = Log(string.Format("Error reading OPB command: instrument {0} out of range", TSI(instrIndex)));
                            throw new OPBException(error);
                        }

                        Instrument instr = context.Instruments[instrIndex];
                        int conn = channelToOffset[channel];
                        int mod = operatorOffsets[channelToOp[channel]];
                        int car = mod + 3;
                        int playOffset = channelToOffset[channel];

                        if (feedconn) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regFeedconn + conn), (byte)instr.FeedConn, context.Time));
                        if (modChr) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regCharacter + mod), (byte)instr.Modulator.Characteristic, context.Time));
                        if (modLvl) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regLevels + mod), modLvlData, context.Time));
                        if (modAtk) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regAttack + mod), (byte) instr.Modulator.AttackDecay, context.Time));
                        if (modSus) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regSustain + mod), (byte) instr.Modulator.SustainRelease, context.Time));
                        if (modWav) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regWave + mod), (byte)instr.Modulator.WaveSelect, context.Time));
                        if (carChr) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regCharacter + car), (byte)instr.Carrier.Characteristic, context.Time));
                        if (carLvl) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regLevels + car), carLvlData, context.Time));
                        if (carAtk) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regAttack + car), (byte)instr.Carrier.AttackDecay, context.Time));
                        if (carSus) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regSustain + car), (byte)instr.Carrier.SustainRelease, context.Time));
                        if (carWav) AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regWave + car), (byte)instr.Carrier.WaveSelect, context.Time));
                        if (isPlay) {
                            AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regFrequency + playOffset), freq, context.Time));
                            AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(regNote + playOffset), note, context.Time));
                        }

                        break;
                    }

                case opbCmdNoteOn:
                case opbCmdNoteOn + 1:
                case opbCmdNoteOn + 2:
                case opbCmdNoteOn + 3:
                case opbCmdNoteOn + 4:
                case opbCmdNoteOn + 5:
                case opbCmdNoteOn + 6:
                case opbCmdNoteOn + 7:
                case opbCmdNoteOn + 8: {
                        int channel = (baseAddr - 0xD7) + (mask != 0 ? 9 : 0);

                        if (channel < 0 || channel >= numChannels) {
                            var error = Log(string.Format("Error reading OPB command: channel {0} out of range", TSI(channel)));
                            throw new OPBException(error);
                        }

                        byte freqNote0, freqNote1;
                        freqNote0 = ReadByte(context);
                        freqNote1 = ReadByte(context);

                        byte freq = freqNote0;
                        byte note = freqNote1;

                        AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(addr - (opbCmdNoteOn - regFrequency)), freq, context.Time));
                        AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)(addr - (opbCmdNoteOn - regNote)), (byte)(note & 0b00111111), context.Time));

                        if ((note & 0b01000000) != 0) {
                            // set modulator volume
                            byte vol;
                            vol = ReadByte(context);
                            int reg = regLevels + operatorOffsets[channelToOp[channel]];
                            AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)reg, vol, context.Time));
                        }
                        if ((note & 0b10000000) != 0) {
                            // set carrier volume
                            byte vol;
                            vol = ReadByte(context);
                            int reg = regLevels + 3 + operatorOffsets[channelToOp[channel]];
                            AddToBuffer(context, buffer, ref bufferIndex, new OPBCommand((ushort)reg, vol, context.Time));
                        }
                        break;
                    }
            }
        }

        private static void ReadChunk(ReadContext context, OPBCommand[] buffer, ref int bufferIndex) {
            int elapsed, loCount, hiCount;

            elapsed = ReadUint7(context);
            loCount = ReadUint7(context);
            hiCount = ReadUint7(context);

            context.Time += TimeSpan.FromTicks(elapsed * TimeSpan.TicksPerMillisecond);

            for (int i = 0; i < loCount; i++) {
                ReadCommand(context, buffer, ref bufferIndex, 0x0);
            }
            for (int i = 0; i < hiCount; i++) {
                ReadCommand(context, buffer, ref bufferIndex, 0x100);
            }
        }

        private static void ReadOpbDefault(ReadContext context) {
            ReadUint32(context);
            var instrumentCount = ReadUint32(context);
            var chunkCount = ReadUint32(context);

            for (uint i = 0; i < instrumentCount; i++) {
                context.Instruments.Add(ReadInstrument(context));
            }

            var buffer = new OPBCommand[defaultReadBufferSize];
            int bufferIndex = 0;

            for (uint i = 0; i < chunkCount; i++) {
                ReadChunk(context, buffer, ref bufferIndex);
            }

            if (bufferIndex > 0) {
                context.Submit(buffer, bufferIndex, context.ReceiverData);
            }
        }

        private const int rawReadBufferSize = 256;
        private const int rawEntrySize = 5;

        private static void ReadOpbRaw(ReadContext context) {
            TimeSpan time = TimeSpan.Zero;
            var buffer = new byte[rawReadBufferSize * rawEntrySize];
            var commandStream = new OPBCommand[rawReadBufferSize];

            int partialOffset = 0;

            int bytesLeft;
            while ((bytesLeft = context.Stream.Read(buffer, partialOffset, buffer.Length - partialOffset)) > 0) {
                bytesLeft += partialOffset;

                int itemsRead = 0;
                int dataOffset = 0;

                while (bytesLeft >= rawEntrySize) {
                    ushort elapsed = (ushort)((buffer[dataOffset] << 8) | buffer[dataOffset + 1]);
                    ushort addr = (ushort)((buffer[dataOffset + 2] << 8) | buffer[dataOffset + 3]);
                    byte data = buffer[dataOffset + 4];

                    time += TimeSpan.FromTicks(elapsed * TimeSpan.TicksPerMillisecond);

                    var cmd = new OPBCommand(addr, data, time);
                    commandStream[itemsRead] = cmd;

                    itemsRead++;
                    bytesLeft -= rawEntrySize;
                    dataOffset += rawEntrySize;
                }

                // copy partial item to start
                partialOffset = bytesLeft;
                for (int i = 0; i < partialOffset; i++, dataOffset++) {
                    buffer[i] = buffer[dataOffset];
                }

                context.Submit(commandStream, itemsRead, context.ReceiverData);
            }
        }

        private static void ConvertFromOpb(ReadContext context) {
            if (ReadByte(context) != 'O' || ReadByte(context) != 'P' || ReadByte(context) != 'B' || ReadByte(context) != 'i' || ReadByte(context) != 'n') {
                throw new OPBException("Couldn't parse OPB file; not a valid OPB file");
            }

            var version = ReadByte(context);
            switch (version) {
                case (byte)'1':
                    break;
                default:
                    throw new OPBException("Couldn't parse OPB file; invalid version or version unsupported");
            }

            var terminator = ReadByte(context);
            if (terminator != 0) {
                throw new OPBException("Couldn't parse OPB file; not a valid OPB file");
            }

            OPBFormat fmt = (OPBFormat)ReadByte(context);

            switch (fmt) {
                default:
                    var error = Log(string.Format("Error reading OPB file: unknown format {0}", fmt));
                    throw new OPBException(error);
                case OPBFormat.Default:
                    ReadOpbDefault(context);
                    break;
                case OPBFormat.Raw:
                    ReadOpbRaw(context);
                    break;
            }
        }
    }
}
