# Spio Registry Server

Owns registry write-side publication transports and origin-facing upload behavior.

Do not place client fetch/cache logic here.
Do not place auth tokens, account rules, or write-origin policy resolution here; those belong behind `src/SpioSecurity/` and optional `src-private/`.
