# OPBinaryLib

OPBinaryLib is a C/C++ library for converting a stream of OPL FM synth chip commands to the OPB music format. A fully managed C# port called [OPBinarySharp](https://github.com/Enichan/OPBinaryLib/tree/main/OPBinarySharp) is also included in this repository.

The OPB music format is a format that stores commands for the Yamaha OPL3 chip (Yamaha YMF262) which was used by DOS soundcards and games. It aims to reduce the size of files storing OPL command streams to close to MIDI (usually less than 2x the size of MIDI) while still being fairly straightforward to parse. OPB files tend to be pretty similar in size to gzip compressed VGM (VGZ) files, but don't require a complex decompression algorithm to read, and can be compressed to be far smaller than VGZ files.

Anyone is encouraged to use the format for their own purposes, with or without the provided C code library.

## Generating OPB files from MIDI

Currently the best way to generate OPB files is to use the [CaptureOPL utility](https://github.com/Enichan/libADLMIDI/releases) to generate OPB files from MIDI, MUS, or XMI files. This utility uses a fork of libADLMIDI (original [here](https://github.com/Wohlstand/libADLMIDI)) to capture the OPL output from libADLMIDI's playback and encodes the stream of OPL commands as an OPB music file.

## How to compose OPB files

There are two ways to compose music to be turned into OPB files:

### MIDI

Use the [ADLplug](https://github.com/jpcima/ADLplug/releases) VST with your DAW to compose your MIDI with the same sound banks that are available in the [CaptureOPL utility](https://github.com/Enichan/libADLMIDI/releases). Once you're happy with your music use the utility to convert your MIDI to OPB.

### Trackers

Any OPL3 capable tracker that outputs VGM files will work, such as [Furnace](https://github.com/tildearrow/furnace/releases). Compose your OPL3 song, export to VGM, then convert to OPB.

## How to program with OPB files

- Use opblib.c/opblib.h to convert an OPB file back to a stream of timestamped OPL3 chip commands
- Send chip commands to one of the many available OPL chip emulators[[1]](https://github.com/aaronsgiles/ymfm)[[2]](https://github.com/nukeykt/Nuked-OPL3)[[3]](https://github.com/rofl0r/woody-opl)[[4]](https://github.com/gtaylormb/opl3_fpga/blob/master/docs/OPL3.java)[[5]](https://github.com/mattiasgustavsson/dos-like/blob/main/source/libs/opl.h) and generate samples
- Use a library like [FNA](https://fna-xna.github.io/), [MonoGame](https://www.monogame.net/), or [raylib](https://www.raylib.com/) which allow you to submit buffers of audio samples to play the sound (DynamicSoundEffectInstance in FNA/MonoGame, UpdateAudioStream in raylib)

## Basic library usage

Store your OPL commands as a contiguous array of `OPB_Command` (including the time in seconds for each command), then call `OPB_OplToFile` to write an OPB file.

```c
OPB_OplToFile(OPB_Format_Default, commandArray, commandCount, "out.opb");
```

This'll return 0 on success or one of the error codes in opblib.h otherwise.

To turn OPB data back into a stream of `OPB_Command` values create a function to receive buffered stream data and use `OPB_FileToOpl`:

```c
int ReceiveOpbBuffer(OPB_Command* commandStream, size_t commandCount, void* context) {
    for (int i = 0; i < commandCount; i++) {
        // do things here with commandStream[i]
    }
    return 0;
}

int main(int argc, char* argv[]) {
    OPB_FileToOpl("in.opb", ReceiveOpbBuffer, NULL);
}
```

Optionally you can pass in a `void*` pointer to user data that will be sent to the receiver function as the `context` argument.

Set `OPB_Log` to a logging implementation to get logging.

The OPB2WAV converter serves as a fully documented sample for reading an OPB file, generating audio via an OPL chip emulator, and storing that as a WAV file.

## How does OPBinaryLib reduce size

There are two main approaches to reducing the size of a stream of OPL3 commands that OPBinaryLib uses.

The first is a simple change that can be made because commands are stored in chunks, with each chunk of commands taking place at the same time. OPL commands typically store the register to write to as a 16-bit integer, with 0x00XX for the first 9 channels and 0x01XX for the final 9 channels. OPB instead stores a count of low (0x00XX) and high (0x01XX) register commands with each chunk, which means that each individual command only needs to store the low 8-bits of its register, which saves 1 byte per command, which means commands are 1/3rd the size.

The second approach involves a bank of "instruments". An instrument in OPB terms is a set of 9 properties: feedback/connection, and the characteristic, attack/decay, sustain/release, and wave select properties for both the modulator and carrier. Setting all of these properties using regular commands would take up a whopping 27 bytes of storage. For each chunk of commands OPBinaryLib detects commands to set these properties for a single channel, and aggregates them into a single instrument which is stored near the start of an OPB file. Then to set the instrument for a channel, OPBinaryLib encodes a single special command (special commands use the unused 0xD0 through 0xDF registers) which is between 4 and 9 bytes long that specifies the instrument to use and which of its properties to set.

Because only a subset of properties for an instrument can be set, a partial match can still use an existing instrument. Additionally, because setting an instrument's properties will often be accompanied by carrier and modulator levels (aka volume) these can optionally be encoded in the command, which saves additional bytes. Finally, because setting these properties often comes before a note command, there's another special command which sets the instrument and takes the note and frequency data, saving yet another 4 bytes.

There are some additional, though somewhat less potent strategies employed to reduce size. One is the "combined note" special command which combines the note and frequency commands into one 3 byte command, saving 1 byte over performing them separately. Finally values larger than a single byte (elapsed time, instrument indices, command counts) outside the header are encoded using a variable length integer, so low values (below 128) need only 1 byte of storage instead of 2 or 4.

## Projects that support OPB files

- [dos-like](https://github.com/mattiasgustavsson/dos-like) (C): dos-like is a programming library/framework, kind of like a tiny game engine, for writing games and programs with a similar feel to MS-DOS productions from the early 90s