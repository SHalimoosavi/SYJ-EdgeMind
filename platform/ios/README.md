# iOS Platform Layer

**Status: not yet implemented — planned for Phase 6 (iOS Native Bridge) and Phase 7 (iOS Minimal UI).**

This directory will hold the Objective-C++ bridge (`EdgeMindBridge.h` / `.mm`) and, later, a minimal SwiftUI app. Both will call the same shared C API in `src/api/edge_mind_api.h` — no separate inference implementation for iOS. Design constraints to respect once this lands: iOS app-sandboxed storage only, explicit handling of memory pressure and app-lifecycle events, no background-execution assumptions.

No bridge files are created yet; placeholder stub headers presented as working code would violate this project's "no fake implementation" rule.
