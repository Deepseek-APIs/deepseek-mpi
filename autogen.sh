#!/bin/sh
set -e

command -v autoreconf >/dev/null 2>&1 || {
  printf 'autoreconf is required but not found.\n' >&2
  exit 1
}

mkdir -p m4
autoreconf --install --force --verbose
printf '\nNow run ./configure && make\n'
