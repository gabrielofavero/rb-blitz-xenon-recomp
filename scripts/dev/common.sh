# Shared command names. Quoted arrays preserve arguments containing spaces.
_rbblitz_dev() {
    case "${1:-help}" in
        build-game) shift; set -- build "$@" ;;
        dev-help) shift; set -- help "$@" ;;
        run-game-normal) shift; set -- run-normal "$@" ;;
        run-game-window) shift; set -- run-window "$@" ;;
        run-game-no-dlc) shift; set -- run-no-dlc "$@" ;;
        run-game-mobile) shift; set -- run-mobile "$@" ;;
        run-game-quick) shift; set -- quick "$@" ;;
    esac
    case "$RBBLITZ_DEV_PLATFORM:${1:-help}" in
        *:build-ios|ios:build|ios:build-installer|ios:installer)
            shift
            _rbblitz_build_ios "$@"
            return
            ;;
    esac
    if [ "$RBBLITZ_DEV_PLATFORM" = windows ]; then
        local shell
        if command -v pwsh >/dev/null 2>&1; then shell=pwsh
        elif command -v powershell.exe >/dev/null 2>&1; then shell=powershell.exe
        else printf 'PowerShell is required on Windows.\n' >&2; return 1; fi
        "$shell" -NoProfile -ExecutionPolicy Bypass -File \
            "$RBBLITZ_DEV_ROOT/scripts/dev/dev-windows.ps1" "$@"
    else
        local python
        if command -v python3 >/dev/null 2>&1; then python=python3
        elif command -v python >/dev/null 2>&1; then python=python
        else printf 'Python 3.11+ is required.\n' >&2; return 1; fi
        "$python" "$RBBLITZ_DEV_ROOT/scripts/dev/dev.py" "$RBBLITZ_DEV_PLATFORM" "$@"
    fi
}
_rbblitz_build_ios() (
    local config="$RBBLITZ_DEV_ROOT/dev.ios.local.sh"
    if [ -f "$config" ]; then
        . "$config" || return
    fi
    sh "$RBBLITZ_DEV_ROOT/scripts/ios/build.sh" "$@"
)
build-game() { _rbblitz_dev build "$@"; }
build-ios() { _rbblitz_dev build-ios "$@"; }
run-game() { _rbblitz_dev run "$@"; }
run-game-normal() { run-game --normal "$@"; }
run-game-window() { run-game --window "$@"; }
run-game-no-dlc() { run-game --no-dlc "$@"; }
run-game-mobile() { run-game --mobile "$@"; }
run-game-quick() { run-game --quick "$@"; }
build-installer() { _rbblitz_dev build-installer "$@"; }
dev-help() { _rbblitz_dev help; }
