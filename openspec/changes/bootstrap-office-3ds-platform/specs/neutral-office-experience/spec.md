## ADDED Requirements

### Requirement: Canonical dashboard and recognition models
The public API SHALL represent a profile, worklog days, absences, activity events, incremental load
progress, bounded recognition requests, and explicit unauthorized, unavailable, and invalid-response
results without product-specific terminology.

#### Scenario: Report a partial dashboard load
- **WHEN** an adapter completes one operation in a multi-operation load
- **THEN** it reports neutral stage, completed/total progress, result, finish state, and any available snapshot

### Requirement: Token-free adapters
Product adapters SHALL receive only canonical request executors and SHALL never receive, store, or
inject bearer credentials or origins. The direct API client SHALL be the single component that joins
the configured origin and injects exactly one runtime bearer header.

#### Scenario: Adapter supplies authorization
- **WHEN** an adapter includes an authorization header in an API request
- **THEN** the direct API client rejects the request before transport execution

### Requirement: Safe direct request execution
The direct API client SHALL reject empty credentials, CR/LF headers, adapter authorization, absolute
or scheme-relative paths, traversal, embedded query/fragment syntax, and zero response bounds. It
SHALL URL-encode query keys and values and pass the declared response bound to transport.

#### Scenario: Execute a valid request
- **WHEN** an adapter submits a contained path, bounded response size, and ordinary headers/query
- **THEN** transport receives the configured origin, encoded query, one bearer header, and the same response bound

### Requirement: Established native presentation remains shared
The public platform SHALL preserve the pre-extraction native composition, bitmap font, touch-control
geometry, top-screen hub and detail hierarchy, persistent lower-screen profile, stereoscopic scenes,
and audio and camera lifecycles. Extraction SHALL NOT replace those shared behaviors with a simplified
or system-font interface. Bitmap glyphs SHALL render only at integer 1x, 2x, or 3x scale with nearest
filtering and integer-aligned screen positions.

#### Scenario: Build a neutral product
- **WHEN** a product supplies neutral presentation inputs
- **THEN** the application retains the established layout, controls, font rendering, scene behavior, and resource bounds
- **AND** only product-owned identity, copy, palette, and brand-bearing assets differ

### Requirement: Product-owned visual identity
The product contract SHALL supply all brand-bearing presentation inputs, including palette and copy,
launcher icon, GUI images, and any logo or mark displayed by a diorama or model. Shared renderers SHALL
not contain a product name, logo, endpoint, product color, or product-specific image.

#### Scenario: Switch products without patching the platform
- **WHEN** two product packages provide different identities and presentation assets
- **THEN** both products use the same shared rendering and interaction implementation
- **AND** each generated application displays only its own supplied identity

### Requirement: First-class pairing methods
QR scanning and manual pairing-code entry SHALL be presented as equally valid ways to submit the same
single-use pairing offer. User-visible copy and public API names SHALL use `pairing code`, not
`fallback code`.

#### Scenario: Choose a pairing method
- **WHEN** the login view is shown
- **THEN** it offers `Scan QR (X)` and a manual pairing-code action in the relevant controls
- **AND** it does not duplicate those input hints in a generic footer legend

### Requirement: Browser and console bridge status
The shared bridge SHALL expose a neutral browser status page for the active pairing session and SHALL
also provide a concise readable console summary. The page and console SHALL use generated product
presentation inputs without exposing the bearer credential or private local values. Each pairing
offer SHALL remain single-use, while the bridge process SHALL be able to rotate the offer without a
restart. The browser page SHALL show the remaining offer lifetime, provide a manual rotation action,
and automatically rotate an unclaimed offer after its maximum 15-minute lifetime. Offer rotation
SHALL NOT extend the authoritative bearer-credential expiry.

#### Scenario: Start a pairing session
- **WHEN** the bridge creates an active pairing offer
- **THEN** the operator can view its QR and pairing code in the browser status page
- **AND** the page shows a countdown and an action that immediately replaces the active offer
- **AND** the console provides the same non-secret operational status without private integration details

#### Scenario: Pairing offer reaches its timeout
- **WHEN** an unclaimed pairing offer reaches its configured lifetime while the bearer credential remains valid
- **THEN** the running bridge creates a new single-use offer without requiring a process restart
- **AND** the browser page displays the new offer and restarts its countdown

#### Scenario: Pairing offer is claimed
- **WHEN** a device successfully consumes the active offer
- **THEN** that offer cannot be consumed again
- **AND** the browser page reports completion and allows the operator to create another single-use offer

#### Scenario: Device clock uses local wall time
- **WHEN** the bridge and device clocks represent the same instant with different epoch offsets
- **THEN** the protected claim supplies an authenticated server-time reference
- **AND** the device evaluates and persists the authoritative credential expiry using that reference
