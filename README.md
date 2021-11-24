## Overview

encjson is a C library for Linux-like operating systems that offers a
JSON encoding and decoding.

## Building

encjson uses [SCons][] and `pkg-config` for building.

Before building encjson for the first time, run
```
git submodule update --init
```

To build encjson, run
```
scons [ prefix=<prefix> ]
```
from the top-level encjson directory. The optional prefix argument is a
directory, `/usr/local` by default, where the build system installs
encjson.

To install encjson, run
```
sudo scons [ prefix=<prefix> ] install
```

## Documentation

The header files under `include` contain detailed documentation.

[SCons]: https://scons.org/
