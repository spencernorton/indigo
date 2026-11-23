#!/bin/sh
# Print the GitHub Release notes for one tag: its CHANGELOG.md section, then how
# to install it. A pre-release (vX.Y.Z-beta.N or vX.Y.Z-rc.N) uses the vX.Y.Z
# section when the changelog has one yet, else the "## Unreleased" section.
# Usage: buildtools/release_notes.sh <tag>   (run from the repository root)
set -eu

tag=${1:?usage: buildtools/release_notes.sh <tag>}
version=${tag%%-*}

section() { # section <heading word>: the lines under "## <word> ..." up to the next "## "
	awk -v v="$1" '/^## / { if (found) exit; if ($2 == v) { found = 1; next } } found { print }' CHANGELOG.md
}

notes=$(section "$version")
if [ -z "$notes" ] && [ "$tag" != "$version" ]; then
	notes=$(section Unreleased)
fi
[ -n "$notes" ] || { echo "release_notes: CHANGELOG.md has no section for $version" >&2; exit 1; }

case $tag in
	*-rc.*) printf '%s\n\n' "> **Release candidate.** $version as it is meant to ship, for a last round of testing before it does. The current stable release is linked from https://norvitech.com/indigo/." ;;
	*-*) printf '%s\n\n' "> **Beta.** A pre-release for testing what comes next. The current stable release is linked from https://norvitech.com/indigo/." ;;
esac
# The notes are a copy of CHANGELOG.md, whose relative links name files in the
# repository. GitHub's release page and feed rewrite them, but the API and gh
# pass them on as written, so each goes to the file at this tag by full URL
# (a folder, ending in /, to its tree).
repo=https://github.com/spencernorton/indigo
printf '%s\n' "$notes" | sed -e '/./,$!d' | sed -E \
	-e "s#\]\(([A-Za-z0-9._][^):]*/)\)#]($repo/tree/$tag/\1)#g" \
	-e "s#\]\(([A-Za-z0-9._][^):]*)\)#]($repo/blob/$tag/\1)#g"
cat <<NOTES

## Install

Follow the **[install guide](https://norvitech.com/indigo/guide/install/)**: pick what starts your GameCube, then four steps. In short: download **\`Indigo-$tag.zip\`** from Assets below (not the Source code ZIP), unzip it on your computer, back up the card's \`swiss\` folder (Swiss hides it) and any \`ipl.dol\` or \`boot.dol\` you replace, then copy two files onto the card.

| What starts your GameCube? | Copy \`ipl.dol\` to | Indigo starts |
| --- | --- | --- |
| GC Loader | \`boot.dol\` at the top of the card, keeping \`boot.iso\` (Swiss starts it) | When you switch on |
| PicoBoot or PicoLoader | \`ipl.dol\` at the top of the card, replacing the old one | When you switch on |
| FlippyDrive | \`boot.dol\` at the top of its microSD card | When you switch on |
| Something else, or not sure | \`apps/indigo/default.dol\` (or install via [Dolphin Depot](https://dolphindepot.org/)) | From Swiss or Dolphin Depot: open \`apps\`, then \`indigo\` |

The second file is \`swiss/patches/apploader.img\`: copy it into the card's \`swiss/patches\` folder (make the folders if needed), replacing the old one, so In-Game Reset (which defaults to Apploader) returns to Indigo out of the box. Any setup can use \`apps/indigo/default.dol\` (or \`apps/indigo.dol\`) and keep starting as it does today. Some PicoBoot and PicoLoader chips have Swiss built in and never read \`ipl.dol\`: the guide shows how to check.

**Updating:** back up the card's \`swiss\` folder, rename the new \`ipl.dol\` to the name of the Indigo file you start now (\`ipl.dol\`, \`boot.dol\`, \`apps/indigo/default.dol\` or \`indigo.dol\`) and copy it over that file. Then copy the new \`swiss/patches/apploader.img\` over the old one.

**Posters and cheats** (optional; unzip one and copy the files in its \`swiss/ui\` or \`swiss/cheats\` folder into the card's folder of the same name, making it if needed): [Posters](https://indigo.norvitech.com/indigo-posters-all.zip) (every region, with Spotlight's stills/descriptions and Jewel Case's disc art) · [Cheats](https://indigo.norvitech.com/indigo-cheats.zip)

For Jewel Case, copy the poster pack's \`swiss/ui/discs.pak\` onto the card
holding your games too, even if Indigo starts on another card. Updating
Indigo's executable alone does not install disc art. Restart after updating
the pack. Games absent from the pack keep the cover-image fallback.

**Verify:** \`SHA256SUMS.txt\` lists the zip's SHA-256, and the zip carries a build provenance attestation: \`gh attestation verify Indigo-$tag.zip --repo spencernorton/indigo\`.
NOTES
