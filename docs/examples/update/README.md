# Example update files

`stable.txt` here is an example of the update list Settings > Network >
**Check for updates** and **Update from SMB share** read, with made-up
values (its hashes match nothing). A release's real files are written by
`buildtools/update_manifest.py` from the release zip and attached to the
GitHub Release: `indigo-<tag>.dol`, `apploader-<tag>.img`, and `stable.txt`
for a `vX.Y.Z` tag or `rc.txt` for a `vX.Y.Z-beta.N` or `vX.Y.Z-rc.N` tag.

Serve the three files side by side as plain static files over HTTP and set
Settings > Setup > Network > **Update Server** to that computer's IPv4
address, or put them in `indigo-updates/` at the top of the SMB share the
same page names. The format, and why it is this shape, is
`docs/dev/software-update-design.md`.
