# Contributor guidance

## Project

office-3ds is a neutral C++17 platform for building small office dashboards for Nintendo 3DS.
It is an application experiment, not a game. Product identity, presentation, backend routes, and
optional integration code enter through a versioned product package; reusable runtime code stays
product-neutral.

## Workflow

- OpenSpec artifacts in `openspec/` are authoritative for behavior and architecture changes.
- Use `just` recipes as the contributor entrypoint when a matching recipe exists.
- Keep host-only dependencies out of Nintendo 3DS and bridge runtime targets.
- Keep generated files in `build/` or `dist/`; only `dist/.gitkeep` may be tracked.
- Keep local values under `.local/` and exclude that directory only through
  `.git/info/exclude`; do not add it to the shared `.gitignore`.
- Never place bearer tokens, private URLs, pairing secrets, raw upstream payloads, machine paths,
  or private integration instructions in source, examples, logs, generated diagnostics, or UI.
- Install Nintendo 3DS dependencies through devkitPro pacman only.
- Pin portable source dependencies to immutable versions and hashes and record their licenses in
  `NOTICE`.
- Use two-space indentation in CMake and `.clang-format` for C and C++.
- Do not commit, push, change remotes, or publish without explicit approval.

## Verification

Run `just test`, `just format-check`, `just lint`, and strict OpenSpec validation for affected
changes. Runtime, rendering, input, camera, and audio changes also require Azahar verification.

<!-- arcantry:start -->
## Arcantry

Use `arcantry.toml` for shared Arcantry configuration.
Treat configured OpenSpec sources as accepted product and engineering intent.
Use configured todo.txt sources for quick intake and changelog sources for consumer-facing release history.
<!-- arcantry:end -->
