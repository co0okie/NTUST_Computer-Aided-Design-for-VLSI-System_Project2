# Pinned HiGHS static dependency

Version: v1.15.1, upstream https://github.com/ERGO-Code/HiGHS/tree/v1.15.1.
Source archive SHA-256:
`a840d269dff2fafb371dd247df13ad5e026d7ce3b35ad3dc1eedd59bf0c2fb16`.

The checked-in x86-64 archives use the GNU C++11 ABI and libstdc++. They were
built on Rocky Linux 9 (glibc 2.34), using Clang 21 and GCC 11's standard library,
with portable CPU defaults and no LTO. They link successfully with GCC 11.
Ubuntu 24.04 supplies newer glibc/libstdc++. On 2026-10-13, a network-disabled
Ubuntu 24.04 Podman container linked these archives with GCC 13.3 and passed
the project's LP, annealing, solver and CLI tests. See
[environment validation](../../../docs/grader-environment.md) for conditions
and the distinction between container validation and the actual grading VM.

The default project configure/build only imports these libraries, with no
network access or third-party compilation. `libhighs_extras.a` is the small API
stub required by this version, even with HiPO disabled. HiPO, GPU, Zlib,
OpenBLAS, examples and upstream tests are disabled. Upstream copyright and
third-party notices are preserved alongside the libraries.

To regenerate outside the assignment's 60-second build window:

```sh
cmake -S HiGHS-1.15.1 -B highs-build \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_INSTALL_PREFIX="$PWD/highs-install" \
  -DBUILD_SHARED_LIBS=OFF -DBUILD_SHARED_EXTRAS_LIB=OFF \
  -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DBUILD_CXX_EXE=OFF \
  -DZLIB=OFF -DHIPO=OFF
cmake --build highs-build -j2
cmake --install highs-build
```

Copy the installed headers (including generated `HConfig.h` and
`highs_export.h`) into `include/`, and both static archives into `lib/`.
Record the compiler/platform and update `SHA256SUMS` after regeneration.
The checksums can be verified from the project root with
`sha256sum -c src/vendor/highs/SHA256SUMS`.
