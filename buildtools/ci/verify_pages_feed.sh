#!/bin/sh
# Check the signed files in the commit GitHub Pages actually built. The runner
# can reach GitHub over HTTPS. The console's public HTTP route is tested separately.
set -eu
tag=$1
signed=$2
if ! printf '%s\n' "$tag" | grep -Eqx 'v[0-9]+\.[0-9]+\.[0-9]+(-(beta|rc)\.[0-9]+)?'; then
    echo 'Invalid Indigo release tag' >&2
    exit 1
fi
channel=stable
case "$tag" in *-*) channel=rc ;; esac
assets="$channel.signed $channel.txt indigo-$tag.dol apploader-$tag.img"
for asset in $assets; do
    test -s "$signed/$asset" || { echo "Missing release file: $asset" >&2; exit 1; }
done

download=$(mktemp)
trap 'rm -f "$download"' EXIT
matches_release() {
    for asset in $assets; do
        url="https://raw.githubusercontent.com/$GITHUB_REPOSITORY/$1/updates/$asset"
        curl -fsS --max-time 25 --output "$download" "$url" || return 1
        cmp -s "$download" "$signed/$asset" || return 1
    done
}

for attempt in $(seq 1 24); do
    if receipt=$(gh api "repos/$GITHUB_REPOSITORY/pages/builds/latest" \
        --jq 'if .status == "built" then .commit else .status end') &&
        printf '%s\n' "$receipt" | grep -Eqx '[0-9a-f]{40}' && matches_release "$receipt"; then
        echo "GitHub Pages built $receipt with all four signed feed files for $tag."
        exit 0
    fi
    [ "$attempt" -eq 24 ] || sleep 10
done
echo "Pages did not report a successful deployment containing $tag (last receipt: $receipt)." >&2
exit 1
