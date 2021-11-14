# OPBinaryLib
C/C++ library for converting a stream of OPL FM synth chip commands to the OPB music format

Basic usage (better documentation coming once the cat stops being crazy) is to store your OPL commands as a contiguous array of OPB_Command (including the time in seconds for each command), then calling OPB_OplToFile:

```c
OPB_OplToFile(OPB_Format_Default, commandArray, commandCount, "out.opb");
```

This'll return 0 on success or one of the error codes in opblib.h otherwise.

Set OPB_Log to a logging implementation to get logging.

## Is this production ready?

Nope
