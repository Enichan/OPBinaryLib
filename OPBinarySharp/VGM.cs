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
using System.IO;
using System.Collections.Generic;
using System.Globalization;

namespace OPBinarySharp {
    public class VGM {
        private const int vgmSampleRate = 44100;

        public static List<OPBCommand> Parse(string filePath) {
            using (var stream = File.OpenRead(filePath)) {
                return Parse(stream);
            }
        }

        public static List<OPBCommand> Parse(Stream stream) {
            using (var reader = new BinaryReader(stream)) {
                return Parse(reader);
            }
        }

        public static List<OPBCommand> Parse(BinaryReader reader) {
            if (reader.ReadByte() != 'V' || reader.ReadByte() != 'g' || reader.ReadByte() != 'm' || reader.ReadByte() != ' ') {
                throw new OPBException("Not a VGM file");
            }

            var length = reader.BaseStream.Position + reader.ReadUInt32();
            var version = reader.ReadUInt32();
            if (version < 0x151) {
                // YMF262 support was added in 1.51
                throw new OPBException("Unsupported VGM version");
            }

            reader.ReadUInt32(); // SN76489 clock
            reader.ReadUInt32(); // YM2413 clock 

            var gd3Offset = reader.BaseStream.Position + reader.ReadUInt32(); // Offset to description tag
            var totalSamples = reader.ReadUInt32(); // Total of all wait values in the file
            var loopOffset = reader.BaseStream.Position + reader.ReadUInt32(); // Relative offset to loop point, or 0 if no loop
            var loopSamples = reader.ReadUInt32(); // Number of samples in one loop, or 0 if there is no loop. Total of all wait values between the loop point and the end of the file

            reader.ReadUInt32(); // "Rate" of recording in Hz, used for rate scaling on playback. It is typically 50 for PAL systems and 60 for NTSC systems
            reader.ReadUInt32(); // SN FB, SNW, SF 
            reader.ReadUInt32(); // YM2612 clock 
            reader.ReadUInt32(); // YM2151 clock 

            var dataOffset = reader.BaseStream.Position + reader.ReadUInt32();

            // A VGM file parser should be aware that some tools may write invalid loop offsets, resulting in out-of-range file offsets or
            // 0-sample loops and treat those as "no loop". (and possibly throw a warning)
            if (loopOffset < dataOffset || loopOffset >= length) {
                loopOffset = 0;
                loopSamples = 0;
            }

            reader.ReadUInt32(); // Sega PCM clock  
            reader.ReadUInt32(); // SPCM Interface  

            reader.BaseStream.Seek(0x50, SeekOrigin.Begin);

            if (length < 0x60) {
                throw new OPBException("Not a valid OPL3 VGM file");
            }

            var opl2Clock = reader.ReadUInt32(); // YM3812 clock 
            var opl1Clock = reader.ReadUInt32(); // YM3526 clock 
            var msxClock = reader.ReadUInt32(); // Y8950 clock (MSX-AUDIO, OPL with ADPCM)
            var opl3Clock = reader.ReadUInt32(); // YMF262 clock

            var clock = Math.Max(msxClock, Math.Max(Math.Max(opl1Clock, opl2Clock), opl3Clock));
            if (clock == 0) {
                throw new OPBException("Not a valid OPL3-compatible VGM file");
            }

            float volume = 1;

            if (dataOffset >= 0x80) {
                int volumeMod = reader.ReadByte();
                if (volumeMod > 0xC0) {
                    volumeMod = -64 + volumeMod - 0xC0;
                }
                if (volumeMod == -63) {
                    volumeMod = -64;
                }
                volume = (float)Math.Pow(2, volumeMod / 0x20);
            }

            reader.BaseStream.Seek(dataOffset, SeekOrigin.Begin);

            int sample = 0;
            bool end = false;
            var commands = new List<OPBCommand>();

            while (!end && reader.BaseStream.Position < length) {
                var cmd = (VGMCommand)reader.ReadByte();

                switch (cmd) {
                    default:
                        throw new OPBException(string.Format("Unsupported VGM command 0x{0}", ((int)cmd).ToString("X2", CultureInfo.InvariantCulture)));

                    case VGMCommand.EndOfData:
                        end = true;
                        break;

                    case VGMCommand.Wait:
                        sample += reader.ReadUInt16();
                        break;

                    case VGMCommand.Wait50:
                        sample += 882; // 1/50th of a second
                        break;
                    case VGMCommand.Wait60:
                        sample += 735; // 1/60th of a second
                        break;

                    case VGMCommand.Wait1:
                    case VGMCommand.Wait2:
                    case VGMCommand.Wait3:
                    case VGMCommand.Wait4:
                    case VGMCommand.Wait5:
                    case VGMCommand.Wait6:
                    case VGMCommand.Wait7:
                    case VGMCommand.Wait8:
                    case VGMCommand.Wait9:
                    case VGMCommand.Wait10:
                    case VGMCommand.Wait11:
                    case VGMCommand.Wait12:
                    case VGMCommand.Wait13:
                    case VGMCommand.Wait14:
                    case VGMCommand.Wait15:
                    case VGMCommand.Wait16:
                        sample += 1 + cmd - VGMCommand.Wait1;
                        break;

                    case VGMCommand.YM3812:
                    case VGMCommand.YM3526:
                    case VGMCommand.Y8950:
                    case VGMCommand.YMF262Port0:
                    case VGMCommand.YMF262Port1: {
                            var time = TimeSpan.FromSeconds(sample / (double)vgmSampleRate);
                            var addr = reader.ReadByte() + (cmd < VGMCommand.YMF262Port0 ? 0 : cmd - VGMCommand.YMF262Port0) * 0x100;
                            var data = reader.ReadByte();
                            commands.Add(new OPBCommand((ushort)addr, (byte)data, time));
                            break;
                        }

                    // data blocks are used by Y8950 chip but aren't OPL compatible so just skip em
                    case VGMCommand.DataBlock: {
                            reader.ReadByte(); // 0x66 compatibility command
                            var size = reader.ReadUInt32();
                            reader.BaseStream.Seek(size, SeekOrigin.Current);
                            break;
                        }

                    // writes data blocks to various places, ignored
                    case VGMCommand.PCMRamWrite:
                        reader.BaseStream.Seek(11, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamSetup:
                        reader.BaseStream.Seek(4, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamSetData:
                        reader.BaseStream.Seek(4, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamFrequency:
                        reader.BaseStream.Seek(5, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamStart:
                        reader.BaseStream.Seek(10, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamStop:
                        reader.BaseStream.Seek(1, SeekOrigin.Current);
                        break;
                    case VGMCommand.DACStreamStartFast:
                        reader.BaseStream.Seek(4, SeekOrigin.Current);
                        break;
                }
            }

            return commands;
        }

        private enum VGMCommand {
            YM3812 = 0x5A,
            YM3526 = 0x5B,
            Y8950 = 0x5C,
            YMF262Port0 = 0x5E,
            YMF262Port1 = 0x5F,
            Wait = 0x61,
            Wait60 = 0x62,
            Wait50 = 0x63,
            EndOfData = 0x66,
            DataBlock = 0x67,
            PCMRamWrite = 0x68,
            DACStreamSetup = 0x90,
            DACStreamSetData = 0x91,
            DACStreamFrequency = 0x92,
            DACStreamStart = 0x93,
            DACStreamStop = 0x94,
            DACStreamStartFast = 0x95,
            Wait1 = 0x70,
            Wait2 = 0x71,
            Wait3 = 0x72,
            Wait4 = 0x73,
            Wait5 = 0x74,
            Wait6 = 0x75,
            Wait7 = 0x76,
            Wait8 = 0x77,
            Wait9 = 0x78,
            Wait10 = 0x79,
            Wait11 = 0x7A,
            Wait12 = 0x7B,
            Wait13 = 0x7C,
            Wait14 = 0x7D,
            Wait15 = 0x7E,
            Wait16 = 0x7F,
        }
    }
}
