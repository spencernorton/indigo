# Update manifest example

`stable.txt` illustrates the signed manifest’s text portion with fictional
hashes. Consoles expect a 64-byte Ed25519 signature followed by those exact
bytes in `stable.signed` or `rc.signed`. To set up a mirror, use the signed
file and versioned DOL from an actual release. See the
[network update guide](../../guide/settings.md#updating-indigo-over-the-network).
