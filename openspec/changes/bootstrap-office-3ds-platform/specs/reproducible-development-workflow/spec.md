## ADDED Requirements

### Requirement: Native generator before cross compilation
The workflow SHALL build product generation with the host compiler before a Nintendo 3DS configure
and SHALL allow the cross build to consume that explicit executable. Cross targets SHALL consume
generated output rather than executing target code.

#### Scenario: Configure a cross product
- **WHEN** a caller supplies a native generator and accepted product package
- **THEN** product generation completes before target compilation without executing a target binary

### Requirement: Immutable portable dependencies
Every fetched portable dependency SHALL use an immutable release archive plus integrity hash or a
full commit identifier and SHALL record license, target reachability, and attribution in `NOTICE`.

#### Scenario: Fetch Lua for code generation
- **WHEN** the host tool is configured
- **THEN** CMake verifies the official archive hash and links Lua only to the generator

### Requirement: Verification before publication
Publication SHALL require strict OpenSpec validation, host builds/tests, deterministic generation,
container checks, Nintendo 3DS build and Azahar verification when present, and text/history/dependency/
visual asset audits. Commits, remotes, and pushes remain separately approval-gated.

#### Scenario: Complete local implementation review
- **WHEN** local implementation is ready but approval has not been given
- **THEN** changes remain uncommitted and no remote state changes
