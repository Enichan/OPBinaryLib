# OPBinarySharp

OPBinarySharp is a C# port of [OPBinaryLib](https://github.com/Enichan/OPBinaryLib). It uses only safe, 100% managed code targeting .NET framework 4.7.2 (4.5)

```csharp
// load OPB file
var commands = OPBinarySharp.OPB.FileToOpl("mysong.opb");

// write OPB file
OPBinarySharp.OPB.OplToFile(OPBFormat.Default, commands, "mysong-clone.opb");
```

# OPBinaryLib

OPBinaryLib is a C/C++ library for converting a stream of OPL FM synth chip commands to the OPB music format.

The OPB music format is a format that stores commands for the Yamaha OPL3 chip (Yamaha YMF262) which was used by DOS soundcards and games. It aims to reduce the size of files storing OPL command streams to close to MIDI (usually less than 2x the size of MIDI) while still being fairly straightforward to parse.

Currently the best way to generate OPB files is to use the [CaptureOPL utility](https://github.com/Enichan/libADLMIDI/releases) to generate OPB files from MIDI, MUS, or XMI files. This utility uses a fork of libADLMIDI (original [here](https://github.com/Wohlstand/libADLMIDI)) to capture the OPL output from libADLMIDI's playback and encodes the stream of OPL commands as an OPB music file.

TL;DR:

- Download [CaptureOPL](https://github.com/Enichan/libADLMIDI/releases)
- Use it to convert a MIDI file to OPB
- Use opblib.c/opblib.h to convert the OPB file back to a stream of timestamped OPL3 chip commands
- Send chip commands to one of the many available OPL chip emulators[[1]](https://github.com/aaronsgiles/ymfm)[[2]](https://github.com/nukeykt/Nuked-OPL3)[[3]](https://github.com/rofl0r/woody-opl)[[4]](https://github.com/gtaylormb/opl3_fpga/blob/master/docs/OPL3.java)[[5]](https://github.com/mattiasgustavsson/dos-like/blob/main/source/libs/opl.h) and generate samples
- Playback audio???
- Profit!

Anyone is encouraged to use the format for their own purposes, with or without the provided C code library.

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

## Projects that support OPB files

- [dos-like](https://github.com/mattiasgustavsson/dos-like) (C): dos-like is a programming library/framework, kind of like a tiny game engine, for writing games and programs with a similar feel to MS-DOS productions from the early 90s