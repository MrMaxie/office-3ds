# Demo server data

The demo product uses `office_3ds_demo_server`, a local reference service backed
by SQLite. Its seed is deterministic and contains fictional workspace data.

Create a short-lived bearer credential in an explicit private file before
starting a client:

```sh
office_3ds_demo_server issue-token \
  --database ./office-3ds-demo.sqlite3 \
  --output ./demo-token.txt \
  --ttl-seconds 3600
```

The command reports only that the file was written. It never prints the bearer
value. The private file contains the opaque token together with its authoritative
expiry so the generated bridge can transfer the minimum credential bundle. Use
`reset` to restore the seed data and revoke all issued credentials.

The server exposes a public `GET /health` endpoint. All `/v1` resources require
the bearer credential. Recognition submissions are bounded and persisted in the
configured SQLite database. A submission identifies its source activity and
recipient, uses one of the bounded values `5`, `3`, or `1`, and carries a
unique `request_id` so retries are idempotent.
