# Unit tests for the pure code (#120)

```sh
cmake -S es-app/tests/unit -B build-tests   # here, not the top level: that needs SDL
cmake --build build-tests --target es-unit-tests
./build-tests/es-unit-tests                 # under a second; --help for options
```

The top level carries them too, under `-DES_BUILD_TESTS=ON` (default OFF, so an
image build never sees them).

**Belongs here:** functions that read nothing, ask nothing and draw nothing -- a
parser, a label, a rule about a string, a schedule. Today `es-app/src/CloudText.cpp`,
`es-app/src/CheevosRetry.cpp` (#175) and `es-app/src/OfflineAchievementsText.cpp`
(#180, the offline proxy's JSON, read with rapidjson -- pass
`-DRAPIDJSON_INCLUDE_DIR=<dir>` on a host without it) and `es-app/src/CheevosIndex.cpp`
(#186 PL-08, what the game index owes a game), and `Utils::String::maskSecrets`
in `es-core/src/utils/StringUtil.cpp` (#177, the credential mask every logged
command line goes through), tested with doctest (`external/doctest/doctest.h`);
the binary compiles those four files, `StringUtil.cpp`, and the tests, and no more.

**Does not:** anything touching `Window`, `Settings`, `SystemConf`, a font, a
file or a script. Extract the pure core, leave the shell where it is, and test
the rest on the VM (`tools/vm-qa`).
