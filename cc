#!/usr/bin/env bash
# Launch Claude Code wired to z.ai's Anthropic-compatible GLM-5.1 endpoint.
#
# Per-launch setup:
#   ./.claude/cc-prompt.md                       — rendered system prompt (template substituted)
#   ~/.claude/projects/<cwd-slug>/memory/        — auto-memory dir, same path CC uses natively
set -eu
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CWD="$(pwd -P)"

# z.ai API key lookup, first match wins:
#   $ZAI_API_KEY  →  ./.env  →  ~/.env  →  $DIR/zai.key  →  $DIR/../zai.key
# The "one level up" fallback lets sibling launchers share a single key file.
#
# We extract the ZAI_API_KEY= line from .env with grep rather than `source`ing
# the file, because sourcing would execute any other commands it contains.
read_dotenv() {
  [ -r "$2" ] || return 0
  local line val
  line=$(grep -E "^[[:space:]]*(export[[:space:]]+)?$1=" "$2" 2>/dev/null | tail -n1) || true
  [ -n "$line" ] || return 0
  val=${line#*=}
  case "$val" in
    \"*\") val=${val#\"}; val=${val%\"} ;;
    \'*\') val=${val#\'}; val=${val%\'} ;;
  esac
  val=${val%$'\r'}
  printf '%s' "$val"
}

: "${ZAI_API_KEY:=}"
if [ -z "$ZAI_API_KEY" ]; then ZAI_API_KEY=$(read_dotenv ZAI_API_KEY "$CWD/.env");  fi
if [ -z "$ZAI_API_KEY" ]; then ZAI_API_KEY=$(read_dotenv ZAI_API_KEY "$HOME/.env"); fi
if [ -z "$ZAI_API_KEY" ] && [ -r "$DIR/zai.key"    ]; then ZAI_API_KEY=$(< "$DIR/zai.key");    fi
if [ -z "$ZAI_API_KEY" ] && [ -r "$DIR/../zai.key" ]; then ZAI_API_KEY=$(< "$DIR/../zai.key"); fi
if [ -z "$ZAI_API_KEY" ]; then
  echo "cc: no z.ai API key found. Set \$ZAI_API_KEY, add ZAI_API_KEY=... to ./.env or ~/.env, or create $DIR/zai.key (or $DIR/../zai.key)." >&2
  exit 1
fi
export ZAI_API_KEY

export ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic
export ANTHROPIC_AUTH_TOKEN="$ZAI_API_KEY"
export ANTHROPIC_DEFAULT_OPUS_MODEL=glm-5.1
export ANTHROPIC_DEFAULT_SONNET_MODEL=glm-5.1
export ANTHROPIC_DEFAULT_HAIKU_MODEL=glm-4.5-air
# Empty capability strings → CC stops sending effort / adaptive_thinking /
# max_effort body fields for these model slots. z.ai's shim doesn't honor them,
# so sending them is at best ignored, at worst confusing to the server.
export ANTHROPIC_DEFAULT_OPUS_MODEL_SUPPORTED_CAPABILITIES=""
export ANTHROPIC_DEFAULT_SONNET_MODEL_SUPPORTED_CAPABILITIES=""
export ANTHROPIC_DEFAULT_HAIKU_MODEL_SUPPORTED_CAPABILITIES=""
# Drop the `thinking:{}` body field. CC's "adaptive" value isn't in z.ai's spec,
# and DISABLE_ADAPTIVE_THINKING alone didn't downgrade it to "enabled" (verified
# via mitm). Sending nothing lets GLM pick its server-side default (thinking ON).
export CLAUDE_CODE_DISABLE_ADAPTIVE_THINKING=1
export CLAUDE_CODE_DISABLE_THINKING=1
# Strip every anthropic-beta: header (claude-code-*, interleaved-thinking-*,
# effort-*, …). Those gate Anthropic-internal features the z.ai shim doesn't
# implement, so sending them is pointless at best.
export CLAUDE_CODE_SIMULATE_PROXY_USAGE=1

# Memory dir: match the path native CC computes, so entries written via the
# wrapper end up alongside entries from plain `claude` runs in the same project.
SLUG="$(echo "$CWD" | sed 's|/|-|g')"           # e.g. /path/to/proj -> -path-to-proj
MEM_DIR="$HOME/.claude/projects/$SLUG/memory"
RENDERED_PROMPT="$CWD/.claude/cc-prompt.md"

mkdir -p "$MEM_DIR" "$(dirname "$RENDERED_PROMPT")"

# The prompt references an absolute memory path, which isn't known until launch
# time. CC doesn't expand variables in --system-prompt-file content, so bake the
# resolved path into a per-project rendered copy ourselves.
sed "s|{{MEMORY_DIR}}|$MEM_DIR|g" "$DIR/claude-code-glm-prompt.md" > "$RENDERED_PROMPT"

exec claude --system-prompt-file "$RENDERED_PROMPT" --disallowed-tools Skill "$@"
