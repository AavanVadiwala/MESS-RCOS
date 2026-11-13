-- MESS BME280 readings for the public website.
-- Run once in the Supabase dashboard: SQL Editor -> New query -> paste -> Run.
-- Safe to re-run. The public (publishable/anon) key can only call the two
-- functions below; it cannot read or change tables directly. Uploads also need
-- the brain's upload key, whose SHA-256 hash is stored in private.upload_keys.

create extension if not exists pgcrypto with schema extensions;
create schema if not exists private; -- not exposed through the Supabase API

create table if not exists public.bme280_readings (
  id bigint generated always as identity primary key,
  measured_at timestamptz not null,
  node_id text not null check (node_id ~ '^[0-9a-f]{12}$'),
  boot_id text not null check (boot_id ~ '^[0-9a-f]{8}$'),
  sequence bigint not null check (sequence >= 0),
  temperature_c real not null check (temperature_c between -40 and 85),
  humidity_pct real not null check (humidity_pct between 0 and 100),
  pressure_hpa real not null check (pressure_hpa between 300 and 1100),
  rssi_dbm smallint check (rssi_dbm between -127 and 0),
  unique (node_id, boot_id, sequence) -- the brain retries batches; duplicates are ignored
);
create index if not exists bme280_readings_measured_at on public.bme280_readings (measured_at);
alter table public.bme280_readings enable row level security; -- no policies: no direct API access

create table if not exists private.upload_keys (
  key_hash text primary key, -- hex SHA-256 of the key in the brain's sdkconfig.local
  label text not null default '',
  created_at timestamptz not null default now()
);

-- A JSON field as a number, or null when it is missing or not a number (never an error).
create or replace function private.num(item jsonb, key text)
returns numeric
language sql immutable set search_path = ''
as $$
  select case when jsonb_typeof(item -> key) = 'number' then (item ->> key)::numeric end
$$;

-- Called by the brain: POST /rest/v1/rpc/add_bme280_readings {"upload_key": "...", "readings": [...]}.
-- Each reading carries age_ms (time since the brain received it) because the brain has no clock.
create or replace function public.add_bme280_readings(upload_key text, readings jsonb)
returns integer
language plpgsql security definer set search_path = ''
as $$
declare
  stored integer;
begin
  if not exists (select 1 from private.upload_keys
                 where key_hash = encode(extensions.digest(upload_key, 'sha256'), 'hex')) then
    raise exception 'invalid upload key' using errcode = '28000';
  end if;
  if jsonb_typeof(readings) <> 'array' or jsonb_array_length(readings) > 200 then
    raise exception 'readings must be an array of at most 200 items' using errcode = '22023';
  end if;
  insert into public.bme280_readings
    (measured_at, node_id, boot_id, sequence, temperature_c, humidity_pct, pressure_hpa, rssi_dbm)
  select now() - make_interval(secs => least(greatest(private.num(r, 'age_ms'), 0), 86400000) / 1000),
         r->>'node_id', r->>'boot_id', private.num(r, 'sequence')::bigint,
         (r->>'temperature_c')::real, (r->>'humidity_pct')::real, (r->>'pressure_hpa')::real,
         nullif(private.num(r, 'rssi_dbm')::smallint, -127)
  from jsonb_array_elements(readings) as r
  -- Skip malformed rows instead of failing the batch, so one bad reading can't block the rest.
  where r->>'node_id' ~ '^[0-9a-f]{12}$' and r->>'boot_id' ~ '^[0-9a-f]{8}$'
    and private.num(r, 'sequence') >= 0 and private.num(r, 'age_ms') is not null
    and private.num(r, 'temperature_c') between -40 and 85
    and private.num(r, 'humidity_pct') between 0 and 100
    and private.num(r, 'pressure_hpa') between 300 and 1100
    and private.num(r, 'rssi_dbm') between -127 and 0
  on conflict (node_id, boot_id, sequence) do nothing;
  get diagnostics stored = row_count;
  -- Keep the free-tier database small: drop readings older than 30 days now and then.
  if random() < 0.01 then
    delete from public.bme280_readings where measured_at < now() - interval '30 days';
  end if;
  return stored;
end
$$;

-- Called by the website: POST /rest/v1/rpc/bme280_series {"window_s": 3600}.
-- Returns, per sensor, columnar averages (at most ~3600 points each) plus link stats for its newest boot.
create or replace function public.bme280_series(window_s integer default 3600)
returns jsonb
language sql stable security definer set search_path = ''
as $$
  with w as (
    select least(greatest(window_s, 60), 7 * 86400) as s
  ), p as (
    select s, greatest(1, ceil(s / 3600.0))::integer as step from w
  ), b as (
    select r.node_id, date_bin(make_interval(secs => p.step), r.measured_at, timestamptz '2000-01-01') as t,
           avg(r.temperature_c) as tc, avg(r.humidity_pct) as h, avg(r.pressure_hpa) as pr
    from public.bme280_readings r, p
    where r.measured_at >= now() - make_interval(secs => p.s)
    group by 1, 2
  ), latest as ( -- newest reading of every sensor seen in the last 7 days
    select distinct on (node_id) node_id, boot_id, measured_at, rssi_dbm
    from public.bme280_readings
    where measured_at >= now() - interval '7 days'
    order by node_id, measured_at desc
  ), session as ( -- readings received vs. expected (one a second) in the last 5 minutes, per sensor.
    -- Time-based because the mesh numbers every sensor's messages together, so sequence gaps mislead.
    select l.node_id, count(*) as received,
           round(extract(epoch from max(r.measured_at) - min(r.measured_at)))::bigint + 1 as sent
    from public.bme280_readings r join latest l on r.node_id = l.node_id and r.boot_id = l.boot_id
    where r.measured_at >= now() - interval '5 minutes'
    group by l.node_id
  ), series as (
    select l.node_id,
      jsonb_build_object(
        'node_id', right(l.node_id, 4), -- public label only; full board IDs (MAC addresses) stay private
        'latest', jsonb_build_object('measured_at', l.measured_at, 'rssi_dbm', l.rssi_dbm,
                    'received_5min', coalesce(s.received, 0),
                    'missed_5min', greatest(coalesce(s.sent - s.received, 0), 0)),
        't', coalesce((select jsonb_agg(extract(epoch from b.t)::bigint order by b.t) from b where b.node_id = l.node_id), '[]'::jsonb),
        'temperature_c', coalesce((select jsonb_agg(round(b.tc::numeric, 2) order by b.t) from b where b.node_id = l.node_id), '[]'::jsonb),
        'humidity_pct', coalesce((select jsonb_agg(round(b.h::numeric, 2) order by b.t) from b where b.node_id = l.node_id), '[]'::jsonb),
        'pressure_hpa', coalesce((select jsonb_agg(round(b.pr::numeric, 2) order by b.t) from b where b.node_id = l.node_id), '[]'::jsonb)
      ) as item
    from latest l left join session s on s.node_id = l.node_id
  )
  select jsonb_build_object(
    'generated_at', now(),
    'window_s', (select s from p),
    'step_s', (select step from p),
    'nodes', coalesce((select jsonb_agg(item order by node_id) from series), '[]'::jsonb))
$$;

revoke all on function public.add_bme280_readings(text, jsonb) from public, anon, authenticated;
revoke all on function public.bme280_series(integer) from public, anon, authenticated;
grant execute on function public.add_bme280_readings(text, jsonb) to anon;
grant execute on function public.bme280_series(integer) to anon;
