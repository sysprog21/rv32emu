# Fuzzing

We are using the [LLVM Fuzzer](https://llvm.org/docs/LibFuzzer.html).

The fuzzer used here is without structured input generation. Instead, we rely
on the fuzzer to mutate the input.

The initial seeds are all the ELF files in the `build` directory.

## Execution

The script compiles the emulator and links it with the LibFuzzer, prepares the seed corpus, and executes the fuzzing tests.

- `.ci/fuzz.sh`

## References

> Inspired by the fuzzer from [libriscv](https://github.com/fwsGonzo/libriscv/tree/master/fuzz).

- [LLVM official LibFuzzer documentation](https://llvm.org/docs/LibFuzzer.html#corpus)
- [Chromium - Getting started with LibFuzzer](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/testing/libfuzzer/getting_started_with_libfuzzer.md)
- [Fuzzing tutorial](https://github.com/google/fuzzing/blob/master/tutorial/libFuzzerTutorial.md)
- [UBSAN](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
