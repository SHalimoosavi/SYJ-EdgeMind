# Scripts

Tooling lives here, kept separate from the inference core (see CONTRIBUTING.md — Python/Node are fine for tooling, never for the inference path).

- `build-windows.ps1` — Phase 5, **implemented**. Authoritative Windows build script: CMake configure + build, with explicit Portable/AVX/AVX2 CPU-mode selection. See `platform/windows/README.md` and `docs/supported-platforms.md`.
- `build-windows.bat` — Phase 5, **implemented**. Thin `cmd.exe` entry point that delegates to `build-windows.ps1` — not a second, independent build implementation.
- `build-ios.sh` — planned, Phase 6.

There is no `download-model.py` or `verify-model.py`, and none is planned. Phase 3 explicitly decided against a model downloader — model acquisition is local-import-only by design (see `docs/model-registry.md`'s "Why local import, not a downloader" section for the reasoning). Model verification is built into the core runtime itself (`src/model/gguf_reader.*`, `model_verifier.*`), not a separate script — every model is verified automatically on import/load, not via a manual tool.
