-- Short-lived launcher hosting grants, independent of Discord accounts.
-- Only token digests and keyed, expiring rate buckets are retained.
CREATE TABLE IF NOT EXISTS weplay_public_publishers (
  owner TEXT PRIMARY KEY REFERENCES weplay_publishers(owner) ON DELETE CASCADE,
  expires_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS weplay_public_publisher_expiry ON weplay_public_publishers(expires_at);
