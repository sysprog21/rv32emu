#!/usr/bin/env bash

# Post the benchmark comparison tables (compare-*.md in the current directory)
# as one comment on a pull request, updated in place on later pushes.
#
# Environment:
#   GH_TOKEN:   token allowed to write pull request comments
#   REPO:       owner/name of the repository
#   PR:         pull request number
#   HEAD_SHA:   pull request head the tables were measured for
#   CONCLUSION: result of the benchmark run
#   RUN_URL:    link to the benchmark run

set -e -u -o pipefail

shopt -s nullglob
present=(compare-*.md)
# No table means the benchmark was skipped for lack of relevant changes, or
# failed before measuring; either way, say nothing.
[ "${#present[@]}" -gt 0 ] || exit 0

MARKER='<!-- rv32emu-benchmark-comparison -->'
{
    echo "${MARKER}"
    echo "## Benchmark comparison against the base commit"
    echo
    echo "Current: ${HEAD_SHA}. Workflow result: ${CONCLUSION} ([details](${RUN_URL}))."
    echo "Measured on one shared runner; treat verdicts as advisory."
    for f in "${present[@]}"; do
        echo
        # Defuse @mentions and cap the size of content this script did not
        # write: by lines, so a table row is rarely cut, and by bytes, so one
        # long line cannot overflow the comment.
        head -n 64 "$f" | head -c 16384 | sed 's/@/@\&#8203;/g'
    done
} > body.md

# Keep a single comment per pull request, updated on each push.
COMMENT=$(gh api --paginate "repos/${REPO}/issues/${PR}/comments" \
    --jq ".[] | select(.user.login == \"github-actions[bot]\" and (.body | startswith(\"${MARKER}\"))) | .id" \
    | head -n 1)
if [ -n "${COMMENT}" ]; then
    gh api -X PATCH "repos/${REPO}/issues/comments/${COMMENT}" -F body=@body.md > /dev/null
else
    gh api -X POST "repos/${REPO}/issues/${PR}/comments" -F body=@body.md > /dev/null
fi
