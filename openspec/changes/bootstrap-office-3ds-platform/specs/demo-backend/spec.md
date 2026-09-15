## ADDED Requirements

### Requirement: Persistent deterministic demo state
The local demo server SHALL migrate a configurable SQLite database using a monotonic schema version,
seed deterministic profile, worklog, absence, and activity data only when absent, and preserve valid
credentials and recognitions across normal restarts.

#### Scenario: Restart with an existing database
- **WHEN** the server restarts with the same database file
- **THEN** schema state, unexpired credentials, and recognitions remain available

### Requirement: Private expiring bearer issuance
The CLI SHALL generate a bounded bearer credential, persist only its hash and authoritative expiry,
write the credential to an explicit private file, and never print the credential. Protected `/v1`
operations SHALL reject missing, unknown, and expired credentials.

#### Scenario: Use an expired credential
- **WHEN** a client calls a protected operation after authoritative expiry
- **THEN** the server returns unauthorized without protected data

### Requirement: Bounded recognition write
The API SHALL persist exactly one recognition only for an existing activity recipient, supported
value, and new request identifier. Invalid or duplicate writes SHALL leave the database unchanged.

#### Scenario: Retry the same request
- **WHEN** a client submits a previously accepted request identifier
- **THEN** the server rejects the duplicate without adding another recognition

### Requirement: Explicit reset and bounded hosting
Reset SHALL recreate seed state and invalidate credentials only when explicitly invoked. Native
Windows/Linux and multi-stage non-root container builds SHALL expose health separately from protected
data and support a persistent database volume.

#### Scenario: Start normally after prior use
- **WHEN** the server starts without the reset command
- **THEN** it does not remove existing recognitions or credentials
