# Spio Registry Client

Owns registry consumption, cache materialization, and snapshot extraction for `file://`, `http://`, and `https://` sources.

Do not place server upload or publish transport code here.
Do not place private trust allowlists, read credentials, or environment-specific registry policy here; those belong behind `src/SpioSecurity/` and optional `src-private/`.
