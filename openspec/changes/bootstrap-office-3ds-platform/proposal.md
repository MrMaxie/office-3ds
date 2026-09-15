## Why

Reusable office-dashboard runtime code needs one public owner and a narrow product boundary. Product
developers need a reproducible way to supply identity, assets, API operations, and exceptional C++
mapping without putting a scripting runtime on Nintendo 3DS. Demo users need a neutral local system
that exercises credential bootstrap, reads, persistence, and one bounded recognition write.

## What Changes

- Establish a portable product API, token-free request executor, and direct API client.
- Add a restricted schema-v1 Lua host that deterministically generates C++ and CMake build inputs.
- Add a C++ adapter escape hatch without arbitrary source overlays.
- Add a neutral product, SQLite developer server, Docker runtime, and reproducible host workflow.
- Track the remaining Nintendo 3DS runtime extraction and generated response codec explicitly.

The project does not provide production hosting, runtime Lua, arbitrary build-script execution, or
product-specific endpoints and branding.

## Capabilities

### New Capabilities

- `product-code-generation`: Versioned package validation and host-only deterministic generation.
- `neutral-office-experience`: Canonical portable dashboard and recognition contracts.
- `demo-backend`: Persistent local reference API and credential workflow.
- `reproducible-development-workflow`: Pinned dependencies, host/target separation, and verification.

## Impact

Affects public headers, host generator, CMake, demo package/server, tests, documentation, and future
Nintendo 3DS runtime targets. Lua is host-generator-only; SQLite and HTTP/JSON libraries are
demo-server-only.
