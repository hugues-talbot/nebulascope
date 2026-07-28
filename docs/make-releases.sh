#!/bin/sh
# Regenerate docs/RELEASES.md from the annotated release tags.
# Run from anywhere inside the repo; commit the result.
cd "$(git rev-parse --show-toplevel)" || exit 1
{
  echo "# Release notes"
  echo
  echo "Generated from the annotated \`v*\` tags (\`docs/make-releases.sh\`)."
  echo
  git tag -l 'v*' --sort=-v:refname \
    --format='## %(refname:short) — %(creatordate:short)%0a%0a%(contents:subject)%0a%0a%(contents:body)'
} > docs/RELEASES.md
echo "wrote docs/RELEASES.md"
