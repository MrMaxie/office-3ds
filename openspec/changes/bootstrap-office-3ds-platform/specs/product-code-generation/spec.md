## ADDED Requirements

### Requirement: Versioned product package
The platform SHALL accept schema version 1 with identity, presentation, assets, backend operations,
authentication policy, local value references, product API version, and adapter selection. It SHALL
reject missing or unknown fields, invalid identifiers, incompatible versions, and paths that resolve
outside the package root before compilation.

#### Scenario: Reject a symbolic-link escape
- **WHEN** an asset or adapter source appears relative but resolves outside the product directory
- **THEN** generation fails without reading or copying the external file

### Requirement: Restricted host-only Lua
The platform SHALL run product Lua only in a bounded native generator without filesystem, process,
package, debug, native-module, environment-initialization, or unrestricted loading APIs. The bridge
and Nintendo 3DS targets SHALL neither link Lua nor load a Lua file.

#### Scenario: Script attempts host access
- **WHEN** a product accesses an unavailable host library or exceeds its instruction budget
- **THEN** generation stops without performing that host operation

### Requirement: Deterministic generated output
Identical product, local values, generator version, and assets SHALL produce byte-identical readable
configuration, request, mapping, asset, and adapter-factory outputs under the build tree without
source-machine absolute paths.

#### Scenario: Generate in two output directories
- **WHEN** the same accepted package is generated twice
- **THEN** every generated file has identical bytes

### Requirement: Declarative operations and C++ escape hatch
Schema v1 SHALL represent methods, paths, query, headers, body, context values, URL encoding,
dependencies, scalar/object/list selectors, optional/default/coalesce, and bounded conversions.
Named dependencies SHALL exist and be acyclic. A product MAY instead supply contained C++ sources
implementing API v1 but SHALL NOT replace shared platform files.

#### Scenario: Reject an unsupported operator
- **WHEN** a mapping uses a constructor outside schema v1
- **THEN** generation fails instead of silently approximating it
