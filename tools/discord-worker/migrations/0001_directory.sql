CREATE TABLE IF NOT EXISTS weplay_publishers (
  owner TEXT PRIMARY KEY,
  token_hash TEXT NOT NULL UNIQUE,
  last_interaction TEXT NOT NULL,
  issued_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS weplay_reports (
  owner TEXT PRIMARY KEY REFERENCES weplay_publishers(owner) ON DELETE CASCADE,
  run_id TEXT NOT NULL,
  sequence INTEGER NOT NULL,
  payload TEXT NOT NULL,
  expires_at INTEGER NOT NULL,
  closed INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS weplay_board (
  slot INTEGER PRIMARY KEY CHECK(slot = 1),
  message_id TEXT,
  digest TEXT,
  lease_until INTEGER NOT NULL DEFAULT 0
);
INSERT OR IGNORE INTO weplay_board(slot) VALUES(1);
