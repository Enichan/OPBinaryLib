# OPBinaryLib
C/C++ library for converting a stream of OPL FM synth chip commands to the OPB music format

## Basic usage

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

Set OPB_Log to a logging implementation to get logging.

## Is this production ready?

Nope
