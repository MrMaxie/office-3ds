## Context

One repository must support multiple products without exposing credentials to adapters or requiring
source overlays. Host generation and Nintendo 3DS cross-compilation cannot share an executable
toolchain. Public examples must remain useful without encoding a private wire contract.

## Decisions

### Product package and generator

Schema v1 uses a Lua table as trusted build description. The embedded Lua host opens no standard
libraries and supplies only constructors for product, request, selector, conversion, context, and URL
encoding nodes. It applies input, memory, nesting, and instruction bounds. Values errors are redacted.
Asset and source paths are canonicalized and contained beneath the package root.

The generator writes only deterministic configuration, request, mapping, asset, and adapter-factory
files in the build tree. Complex products select API-v1 C++ sources instead of extending the mapping
language for a single integration.

### Token-free adapter boundary

An adapter creates a date-scoped `DashboardLoad` and advances it using `RequestExecutor`. It submits
recognition through the same executor. `DirectApiClient` owns origin joining and bearer injection,
rejects adapter authorization, unsafe paths/headers, and propagates bounded response sizes.

### Build topology

A native build produces the generator. A cross build receives its executable path and consumes only
generated C++/CMake plus public headers. Immutable source archives and commits own portable host
dependencies. Target libraries remain provided by devkitPro.

### Demo boundary

The C++17 server owns migrations, deterministic seed state, expiring hashed credentials, and durable
recognitions in SQLite. Its CLI writes credentials to an explicit file and never prints them. Docker
uses a multi-stage, non-root runtime with a persistent volume.

## Risks / Trade-offs

- The schema can grow into a programming language; unsupported one-off behavior moves to C++.
- Generated code can hide behavior; deterministic readable outputs and golden/repeatability tests
  make it reviewable.
- Host/target execution can be confused; cross builds require an explicit native generator.
- A neutral demo can be mistaken for production software; documentation and defaults state its local
developer-only role.

### Pairing bridge lifecycle

A pairing offer is single-use, but the local bridge process is reusable for as long as its loaded
bearer credential remains authoritative. The browser status page renders a server-derived countdown,
rotates an expired unclaimed offer automatically, and exposes a CSRF-protected action that replaces
an active or claimed offer. Rotation invalidates the preceding QR secret and manual pairing code. It
never renews or extends the bearer credential, and the credential remains memory-only until process
shutdown or authoritative expiry.

The protected claim also carries the bridge epoch at issuance. The device derives and persists a
server-to-device clock offset, because Nintendo 3DS wall time may be represented as local time while
backend expiry is UTC. Expiry checks apply that authenticated offset; they do not lengthen the
server-provided credential lifetime.

## Migration Plan

Complete host contracts and demo first, then port reusable runtime, UI, rendering, transport, and
bridge layers behind the stable API. Add console packaging only after generated mapping compiles into
a usable adapter. Verify the complete native flow in Azahar before marking the change complete.
