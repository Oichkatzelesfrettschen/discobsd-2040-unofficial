#!/bin/sh
# Keep machine-specific cache bindings outside the tracked client configuration.
set -eu

: "${PYTHON:?Select the intended Python interpreter}"
: "${GRAFT_CACHE_READER:?Set the absolute path to the guarded cache-reader shell wrapper}"
case "$GRAFT_CACHE_READER" in
/*) ;;
*) printf '%s\n' 'GRAFT_CACHE_READER must be an absolute path' >&2; exit 2 ;;
esac
if [ ! -f "$GRAFT_CACHE_READER" ] || [ -L "$GRAFT_CACHE_READER" ] ||
   [ ! -r "$GRAFT_CACHE_READER" ]; then
    printf '%s\n' 'GRAFT_CACHE_READER must name a readable regular shell wrapper' >&2
    exit 2
fi
export PYTHON
exec /bin/sh "$GRAFT_CACHE_READER" "$@"
