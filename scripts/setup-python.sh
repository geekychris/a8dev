#!/usr/bin/env bash
# Create .venv with the a8dev package (emulator host, CLI, MCP server, DevBench).
source "$(dirname "$0")/lib.sh"

step "python environment (.venv)"
VENV="$A8_ROOT/.venv"
if have uv; then
  [ -x "$VENV/bin/python" ] || uv venv -q --python ">=3.10" "$VENV"
  uv pip install -q --python "$VENV/bin/python" -e "$A8_ROOT/tools/a8dev"
else
  py=""
  for c in python3.13 python3.12 python3.11 python3.10 python3; do
    if have "$c" && "$c" -c 'import sys; sys.exit(sys.version_info < (3,10))' 2>/dev/null; then py="$c"; break; fi
  done
  [ -n "$py" ] || die "need Python >= 3.10 (or install uv: https://docs.astral.sh/uv/)"
  [ -x "$VENV/bin/python" ] || "$py" -m venv "$VENV"
  "$VENV/bin/python" -m pip install -q --upgrade pip
  "$VENV/bin/python" -m pip install -q -e "$A8_ROOT/tools/a8dev"
fi
"$VENV/bin/python" -c "import a8dev.cli, PIL, mcp, starlette, uvicorn" 2>/dev/null >/dev/null \
  || die "python deps failed to import"
ok "a8dev CLI at .venv/bin/a8dev, MCP server at .venv/bin/a8dev-mcp"
