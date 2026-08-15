# third_party/

llama.cpp is **not** vendored into this directory. It is fetched reproducibly
via CMake's `FetchContent`, pinned to the exact tag recorded in
`docs/architecture.md` (`b10375`) — never tracking `main`/`master` — and
lands under the build tree's own dependency area (`build/_deps/llama_cpp-src`
by default), not inside the repository working tree. See the root
`CMakeLists.txt` for the `FetchContent_Declare` block and its comments on
why `SOURCE_DIR` is deliberately left unset.

This directory exists as a reserved location for a manually-vendored
system/offline build (`-DSYJ_EDGEMIND_USE_SYSTEM_LLAMA=ON`), not as the
default fetch target. The `.gitkeep` placeholder keeps the directory
present in git without committing any actual llama.cpp source; `.gitignore`
excludes `*.gguf`/build output from it regardless.
