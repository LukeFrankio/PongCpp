# Documentation Index

Central index for all non-Doxygen documentation in PongCpp. These docs describe gameplay, configuration, architecture, APIs, and contributor guidance.

---

## Sections

| Audience | Document | Purpose |
|----------|----------|---------|
| Players | [User Guide](user/user-guide.md) | How to run/play, modes, controls, recording, troubleshooting |
| Developers | [Architecture Guide](developer/architecture.md) | Internal structure, subsystems, data flow, extensibility |
| Developers | [API Reference](developer/api-reference.md) | Public & semi-public types, functions, settings fields |
| All | Main README (repo root) | Project overview, quick build & feature summary |
| Auto-Generated | Doxygen HTML | Full annotated source API (after build) |

---

## Quick Start

### Users

1. Build or download binaries
2. Run console version (`pong` / `pong.exe`) or GUI (`pong_win.exe`)
3. Read the [User Guide](user/user-guide.md) for modes, controls & recording

### Developers

1. Read [Architecture](developer/architecture.md)
2. Build project (see root README)
3. Explore [API Reference](developer/api-reference.md) for integration points
4. Optionally enable documentation target for Doxygen HTML

---

## Feature Snapshot (See root README for full table)

* Multi-ball & obstacles (combined mode supported)
* Physics toggle (Arcade vs Physical)
* Path tracer with soft shadows, metallic shading, accumulation
* Recording subsystem (fixed-step simulation, FPS selectable)
* AI vs AI spectator mode
* Persistent configurable settings + high scores

---

## Building Docs (Doxygen)

```bash
cmake -S . -B build -DBUILD_DOCUMENTATION=ON
cmake --build build --target docs --config Release
# Open docs/doxygen/html/index.html
```

Targets: `docs`, `clean-docs`.

---

## Contributing Documentation

Guidelines:

* Keep user vs developer scope distinct
* Document new settings & flags when introduced
* Update API reference when adding enums / struct fields
* Provide rationale for non-obvious design choices in architecture doc

---

## Support

Check existing documents, then inspect source (`src/core/game_core.*`) or open an issue (if repository hosting supports it). For rendering questions, see `win/soft_renderer.*` comments.

---

## Versioning

Documentation is updated continuously; refer to commit history or (if present) `CHANGELOG.md` for chronological feature additions.

---

## License

Documentation shares the same license / usage terms as the codebase. Attribution appreciated for derived works.
