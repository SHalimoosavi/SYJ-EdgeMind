# Security Policy

## Threat model (summary)

SYJ EdgeMind runs untrusted quantized model weights and user prompts entirely on-device, offline, with no server component. The main risks in scope are:

- A malicious or corrupted GGUF file causing memory corruption, unbounded allocation, or arbitrary code execution during load or inference.
- A malicious or corrupted model file being loaded silently without the user being aware it failed verification.
- Uncontrolled context/KV-cache growth exhausting device memory (a reliability/DoS concern on 4 GB devices, not just a hardening concern).
- Unintended network activity (SYJ EdgeMind must never phone home; the model *downloader* is a separate, explicit, user-initiated tool from the *inference runtime*).
- Unsafe file handling of model paths/config on both Windows and iOS (including iOS sandbox violations).

Out of scope for now: multi-user/server deployments, remote APIs — this project does not run a network service.

## Guarantees

- No telemetry, no analytics, no hidden networking, no cloud fallback, no API keys, no secrets in the repository.
- Models are verified via SHA-256 before load; a checksum mismatch or missing file is a hard error, never a silent continue.
- Configurations that exceed the configured memory budget are rejected, not clamped silently.
- Full details in [docs/security.md](docs/security.md), maintained as the implementation lands.

## Reporting a vulnerability

Please do not open a public GitHub issue for a suspected vulnerability. Instead, open a private security advisory on the repository (GitHub Security Advisories) or contact the maintainer directly through the repository owner's GitHub profile. Include:

- A description of the issue and its potential impact
- Steps to reproduce (a minimal GGUF/config if the issue is model- or config-triggered)
- Affected platform(s) and llama.cpp commit in use

This is a young, single-maintainer project — please allow reasonable time for a response and fix before any public disclosure.
