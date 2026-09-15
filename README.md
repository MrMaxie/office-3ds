# office-3ds

office-3ds is a C++17 reference platform for building a compact office dashboard for Nintendo 3DS.
The reusable API and transport are product-neutral. A versioned Lua package supplies identity,
presentation, assets, backend operations, and either a generated adapter or explicit C++ adapter
sources. Lua runs only in the native build tool; generated bridge and console code does not load or
link Lua.

The host generator, public API, direct API client, generated credential bridge, protected
credential-claim runtime, product-session lifecycle, canonical generated response codec, neutral
product package, and SQLite demo server build on Windows and Linux. The Nintendo 3DS target includes
the protected QR claim, direct API path, bounded credential storage, neutral dashboard navigation,
recognition, camera, audio, rendering, and orderly service shutdown. Azahar is the manual runtime
verification environment.

## Host build

Requirements: CMake 3.25+, a C++17 compiler, MinGW Makefiles on Windows, and `just` optionally.
Dependencies are fetched from immutable upstream revisions or archives during configuration.

```text
just test
just generate-demo
```

The demo server is a local developer reference. See `products/demo/server/README.md` for its CLI and
container workflow. It is not intended for internet deployment.

After issuing a demo credential, set the generated `OFFICE_3DS_DEMO_TOKEN_FILE` environment variable
to the credential file's absolute path and run `office_3ds_bridge`. The bridge exposes only the
single-use protected claim and stops being part of the data path after a client claims the
credential. Use `--advertise` with the private IPv4 address reachable by the client; the default is
loopback for host testing.

## Nintendo 3DS build

Inside the devcontainer, `just build-3ds` creates a Debug `.3dsx` and
`just build-3ds-release` creates a Release `.3dsx` under `dist/3ds/`. These workflows first build the
native generator, then configure a separate devkitARM tree. The console target consumes only
generated C++; Lua is not a Nintendo 3DS link input.

## Product package

`products/demo/product.lua` is the schema-v1 example. A package contains:

```text
product.lua
assets/
src/                    # optional C++ adapter sources
```

Local values are provided separately and must not contain bearer tokens. The generator accepts only
an absolute product file, an optional values file, and a build output directory. It validates product
paths, schema/API versions, operation dependencies, and required values before producing files under
`build/generated/product/<slug>/`.

Consumers call `office_3ds_add_product(PRODUCT_DIR ... SLUG ... VALUES_FILE ...)`. Cross-compiling consumers
first build `office_3ds_product_codegen` natively and pass its path through `CODEGEN_EXECUTABLE`.
The native tool is emitted under the build tree's stable `bin/` directory; consumers do not depend
on a FetchContent sub-build layout.
An adapter receives a token-free `RequestExecutor`; `DirectApiClient` alone combines the configured
origin and runtime bearer credential.

The generated adapter evaluates the schema-v1 request and selector subset at runtime: context-backed
query, header and JSON body values; field, object and list selection; optional/default/coalesce; and
bounded string, number and date conversion. Products with custom orchestration or wire formats use
the versioned C++ adapter escape hatch.

## Security boundary

- Product Lua has no standard libraries, filesystem, process, package, debug, or native-module API.
- Product adapters cannot provide an authorization header or an external origin.
- Product asset and C++ source paths must resolve beneath the product directory, including through
  symbolic links.
- Bearer credentials are runtime-only inputs and are never accepted by code generation.

See `openspec/changes/bootstrap-office-3ds-platform/` for the authoritative implementation status.

## License

Copyright 2026 MrMaxie. Licensed under Apache-2.0. Third-party notices are recorded in `NOTICE`.
