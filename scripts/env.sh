#!/usr/bin/env bash

if [[ -n "${BASH_VERSION:-}" ]]; then
  CRASH64_ENV_SOURCE="${BASH_SOURCE[0]}"
elif [[ -n "${ZSH_VERSION:-}" ]]; then
  CRASH64_ENV_SOURCE="${(%):-%N}"
else
  CRASH64_ENV_SOURCE="$0"
fi

CRASH64_ROOT="$(cd "$(dirname "$CRASH64_ENV_SOURCE")/.." && pwd)"
export CRASH64_ROOT

# Respect a caller-provided SDK location. The repository-local path is only the
# default, so the same checkout can use a native SDK on Linux, macOS, or WSL.
if [[ -z "${N64_INST:-}" ]]; then
  export N64_INST="$CRASH64_ROOT/.local/tools/n64"
fi
export PATH="$N64_INST/bin:$PATH"
unset CRASH64_ENV_SOURCE

