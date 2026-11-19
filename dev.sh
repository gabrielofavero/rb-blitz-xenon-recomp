#!/usr/bin/env bash
# Source in Bash/zsh to load commands, or execute with an action.
if [ -n "${ZSH_VERSION:-}" ]; then
    eval '_rbblitz_source=${(%):-%x}'
else
    _rbblitz_source=${BASH_SOURCE[0]}
fi
RBBLITZ_DEV_ROOT=$(CDPATH= cd -- "$(dirname -- "$_rbblitz_source")" && pwd)
_rbblitz_platform=${RBBLITZ_DEV_PLATFORM:-}
if [ "${1:-}" = --platform ]; then
    if [ "$#" -lt 2 ]; then
        printf '%s\n' '--platform requires windows, linux, mac, or ios.' >&2
        return 2 2>/dev/null || exit 2
    fi
    _rbblitz_platform=${2:-}
    shift 2
fi
if [ -z "$_rbblitz_platform" ]; then
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*) _rbblitz_platform=windows ;;
        Linux) _rbblitz_platform=linux ;;
        Darwin)
            case "$(uname -m)" in
                iPhone*|iPad*) _rbblitz_platform=ios ;;
                *) _rbblitz_platform=mac ;;
            esac ;;
        *) printf 'Unsupported development host: %s\n' "$(uname -s)" >&2; return 2 2>/dev/null || exit 2 ;;
    esac
fi
case "$_rbblitz_platform" in
    windows|linux|mac|ios) . "$RBBLITZ_DEV_ROOT/scripts/dev/dev-$_rbblitz_platform.sh" ;;
    *) printf 'Unknown platform: %s\n' "$_rbblitz_platform" >&2; return 2 2>/dev/null || exit 2 ;;
esac
if [ -n "${ZSH_VERSION:-}" ]; then
    case "$ZSH_EVAL_CONTEXT" in *:file) _rbblitz_sourced=yes ;; *) _rbblitz_sourced=no ;; esac
elif [ "${BASH_SOURCE[0]}" != "$0" ]; then
    _rbblitz_sourced=yes
else
    _rbblitz_sourced=no
fi
if [ "$_rbblitz_sourced" = no ]; then
    if [ "$#" -eq 0 ]; then set -- help; fi
    _rbblitz_dev "$@"
fi
