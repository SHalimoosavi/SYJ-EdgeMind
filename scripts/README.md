# Scripts

Tooling lives here, kept separate from the inference core (see CONTRIBUTING.md — Python/Node are fine for tooling, never for the inference path).

Planned, not yet implemented:

- `build-windows.ps1` / `build-windows.bat` — Phase 5
- `build-ios.sh` — Phase 6
- `download-model.py` — Phase 3 (explicit, user-initiated model acquisition; never invoked automatically during inference)
- `verify-model.py` — Phase 3 (SHA-256 verification against `models/registry.json`)
