-- Dummy schema so the database initializes. Replace with the real one.
CREATE TABLE IF NOT EXISTS dummy (
    id         SERIAL PRIMARY KEY,
    name       TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
