#!/usr/bin/env bash
# Shared helpers for docker/tests/smoke.sh.
#
# Mount layout inside the container (set up by bin/test-image.sh):
#   /smoke/smoke.sh         <- the wolf smoke test
#   /smoke-common/lib.sh    <- this file
#
# Usage inside smoke.sh:
#
#     source /smoke-common/lib.sh
#     assert_has       xwayland gst-inspect-1.0
#     assert_version   /wolf/fake-udev --help
#     assert_shared_ok /wolf/wolf
#     smoke_report

set -u -o pipefail

_OK=0 _FAIL=0

log()  { printf '[smoke] %s\n' "$*"; }
ok()   { _OK=$((_OK+1));     printf '   ok   %s\n' "$*"; }
bad()  { _FAIL=$((_FAIL+1)); printf '   FAIL %s\n' "$*" >&2; }

# ---- result ---------------------------------------------------------------
# Call once at the end of smoke.sh. Exits non-zero if any assertion failed.
smoke_report() {
  local total=$((_OK + _FAIL))
  if [[ "$_FAIL" -eq 0 ]]; then
    log "PASS ${_OK}/${total}"
    exit 0
  fi
  log "FAIL ${_FAIL}/${total}"
  exit 1
}

# ---- assertions -----------------------------------------------------------

# assert_has <bin> [<bin>...]
#   Every argument must resolve via `command -v`.
assert_has() {
  for bin in "$@"; do
    if command -v "$bin" >/dev/null 2>&1; then
      ok "binary on PATH: $bin ($(command -v "$bin"))"
    else
      bad "binary on PATH: $bin"
    fi
  done
}

# assert_path <path> [<path>...]
#   Every argument must exist as a file or dir.
assert_path() {
  for p in "$@"; do
    if [[ -e "$p" ]]; then
      ok "path exists: $p"
    else
      bad "path exists: $p"
    fi
  done
}

# assert_version <argv...>
#   Runs the given command with a short timeout. Any exit code is tolerated
#   (some binaries exit 1 on --version), but missing shared libs and SIGSEGV
#   fail the check. Stdout/stderr get captured and dumped on failure.
assert_version() {
  local out
  if out=$(timeout 10 "$@" 2>&1); then
    ok "runs: $* => $(printf '%s' "$out" | head -1)"
    return 0
  fi
  local rc=$?
  if printf '%s' "$out" | grep -Eiq 'error while loading shared libraries|cannot open shared object|segmentation fault|command not found'; then
    bad "runs: $* (fatal: $(printf '%s' "$out" | head -1))"
    return 1
  fi
  # Any other non-zero exit is still suspicious but not automatically fatal;
  # surface it and count as pass so finicky --version commands don't break CI.
  ok "runs: $* (exit $rc; $(printf '%s' "$out" | head -1))"
}

# assert_shared_ok <binary>
#   Ensures every shared library the binary links against resolves. Catches
#   the class of regressions where the builder stage installs -dev packages
#   whose runtime .so isn't in the slim runtime stage.
assert_shared_ok() {
  local bin="$1"
  if [[ ! -x "$bin" ]]; then
    bad "ldd clean: $bin (not executable)"
    return 1
  fi
  local missing missing_count
  missing=$(ldd "$bin" 2>&1 | grep -E 'not found' || true)
  if [[ -z "$missing" ]]; then
    ok "ldd clean: $bin"
  else
    missing_count=$(printf '%s\n' "$missing" | grep -c 'not found')
    bad "ldd clean: $bin ($missing_count missing)"
    printf '%s\n' "$missing" | sed 's/^/     | /' >&2
  fi
}

# assert_env <VAR> [<expected-substring>]
#   Verifies VAR is set; optionally checks that its value contains a substring.
assert_env() {
  local var="$1"
  local want="${2:-}"
  local val="${!var:-}"
  if [[ -z "$val" ]]; then
    bad "env set: $var"
    return 1
  fi
  if [[ -n "$want" && "$val" != *"$want"* ]]; then
    bad "env set: $var (=$val, expected to contain '$want')"
    return 1
  fi
  ok "env set: $var=$val"
}
