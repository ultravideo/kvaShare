kvaShare
===============
Distributed encoding framework for kvazaar.

With this one can distribute the encoding of a video to several computers to speedup the process massively.

## Usage
### Master
With -help the program prints out all the commands that can be used in the master program.

#### Mandatory options:
    -i <input_file> -o <output_file> -ip <slaves' IPs> -r <ratios>
#### For example:
    "KvaShare.exe -i input.yuv -o test.265 -ip 127.0.0.1 -r 1"

### Slave
There is no mandatory options for slave program, so if default port is used, one can just start the executable

## Building

- Open kvazaar solution (.sln) under kvazaar/build.
- Select Release and x64 for target.
- Right click kvazaar_lib and select properties.
  - Under General change Configuration Type to "Dynamic Library (.dll)"
  - Under C/C++ -> Preprocessor add "PIC" into the Preprocessor Definitions.
- Right click the kvazaar_lib and select "build".
- When a build is complete, there should be kvazaar_lib.dll and kvazaar_lib.lib in the kvazaar/build/x64-Release-libs.
- At this point one can build both Master and Slave programs of kvaShare.
- To get the executables to work one must copy the kvazaar_lib.dll into the same folder as the executable.
