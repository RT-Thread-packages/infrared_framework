# Infrared framework API Reference {#mainpage}

Source-level API reference for the RT-Thread infrared software package.

The package offers two mutually exclusive build modes: **LEGACY** (single-device
API in `inc/` and `src/`) and **FRAMEWORK** (multi-instance API, hardware backends,
codec adapters and examples in `framework/`). The generated reference documents
both modes; they are **not** linked into one firmware image simultaneously.

- [Structures](annotated.html): public configuration, device and codec types
- [Files](files.html): C source and header browsing
- [Globals](globals.html): functions and definitions
- [Handwritten documentation](https://wdfk-prog.github.io/infrared_framework/): architecture, usage and integration notes

Compilation profiles and verification boundaries are described in `doc/ci.md`.
