# Unit tests for the pure code (#120)

```sh
cmake -S es-app/tests/unit -B build-tests   # here, not the top level: that needs SDL
cmake --build build-tests --target es-unit-tests
./build-tests/es-unit-tests                 # under a second; --help for options
```

The top level carries them too, under `-DES_BUILD_TESTS=ON` (default OFF, so an
image build never sees them).

**Belongs here:** functions that read nothing, ask nothing and draw nothing -- a
parser, a label, a rule about a string. Today `es-app/src/CloudText.cpp`, tested
with doctest (`external/doctest/doctest.h`); the binary compiles that file,
`es-core/src/utils/StringUtil.cpp`, and the tests, and no more.

**Does not:** anything touching `Window`, `Settings`, `SystemConf`, a font, a
file or a script. Extract the pure core, leave the shell where it is, and test
the rest on the VM (`tools/vm-qa`).
