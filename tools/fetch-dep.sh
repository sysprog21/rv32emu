#!/bin/sh
# Fetch one pinned dependency as a shallow clone: fetch-dep.sh DIR URL SHA
#
# The release branch carries no git submodules, so sources that used to be
# submodules are cloned here on first use. A non-empty DIR without .git is
# taken as user-supplied and left alone. The clone is built in a temporary
# directory and renamed into place, so an interrupted fetch cannot leave a
# half-populated DIR that looks complete.

set -eu

dir=$1
url=$2
rev=$3

if [ -e "$dir/.git" ]; then
    # An existing checkout is kept as is; say so when it is not the pinned one.
    [ "$(git -C "$dir" rev-parse HEAD 2> /dev/null)" = "$rev" ] \
        || echo "Warning: $dir is not at $rev; remove it to refetch" >&2
    exit 0
fi
if [ -n "$(ls -A "$dir" 2> /dev/null)" ]; then
    echo "Notice: using existing $dir without .git" >&2
    exit 0
fi

tmp="$dir.tmp.$$"
trap 'rm -rf "$tmp"' EXIT
rm -rf "$tmp"
git init -q "$tmp" \
    && git -C "$tmp" fetch -q --depth=1 "$url" "$rev" \
    && git -C "$tmp" checkout -q FETCH_HEAD \
    && {
        [ -e "$dir/.git" ] || {
            rmdir "$dir" 2> /dev/null
            [ ! -e "$dir" ] && mv "$tmp" "$dir"
        }
    } || [ -e "$dir/.git" ] || {
    echo "Error: Failed to fetch $url at $rev into $dir" >&2
    exit 1
}
