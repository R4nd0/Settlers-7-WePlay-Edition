-- Public metadata and a bounded blind relay. No VPN invitation/private key is stored.
CREATE TABLE IF NOT EXISTS weplay_directory_hosts (
  owner TEXT PRIMARY KEY REFERENCES weplay_publishers(owner) ON DELETE CASCADE,
  run_id TEXT NOT NULL,
  room_id TEXT NOT NULL,
  content_id TEXT NOT NULL,
  network_revision INTEGER NOT NULL,
  version TEXT NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS weplay_join_requests (
  id TEXT PRIMARY KEY,
  owner TEXT NOT NULL,
  run_id TEXT NOT NULL,
  session_id TEXT NOT NULL,
  game_id TEXT NOT NULL,
  token_hash TEXT NOT NULL,
  public_key TEXT NOT NULL,
  challenge TEXT NOT NULL,
  created_at INTEGER NOT NULL,
  expires_at INTEGER NOT NULL,
  envelope TEXT,
  consumed INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS weplay_join_owner ON weplay_join_requests(owner,run_id,expires_at);
CREATE INDEX IF NOT EXISTS weplay_join_expiry ON weplay_join_requests(expires_at);
CREATE TABLE IF NOT EXISTS weplay_join_rates (
  bucket TEXT PRIMARY KEY,
  count INTEGER NOT NULL,
  expires_at INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS weplay_join_rate_expiry ON weplay_join_rates(expires_at);
