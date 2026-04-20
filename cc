#!/usr/bin/env bash
# Launch Claude Code wired to z.ai's Anthropic-compatible GLM-5.1 endpoint.
#
# Per-launch setup:
#   ./.claude/cc-prompt.md                       — rendered system prompt (template substituted)
#   ~/.claude/projects/<cwd-slug>/memory/        — auto-memory dir, same path CC uses natively
set -eu
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CWD="$(pwd -P)"

# Resolve the z.ai API key. Precedence (first hit wins):
#   1. $ZAI_API_KEY in the environment
#   2. ZAI_API_KEY=... in ./.env   (current working directory)
#   3. ZAI_API_KEY=... in ~/.env
#   4. ./zai.key next to this script
#   5. ../zai.key (so sibling launchers can share one key file)
#
# .env parsing is deliberately minimal: `KEY=value`, `KEY="value"`, `KEY='value'`,
# optional leading `export `. No inline comments, no variable interpolation.
read_dotenv() {
  # $1=var-name  $2=file  →  stdout = value (empty if not found)
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
# Declare GLM has none of CC's optional Anthropic capabilities — strip effort/adaptive/max_effort fields & betas
export ANTHROPIC_DEFAULT_OPUS_MODEL_SUPPORTED_CAPABILITIES=""
export ANTHROPIC_DEFAULT_SONNET_MODEL_SUPPORTED_CAPABILITIES=""
export ANTHROPIC_DEFAULT_HAIKU_MODEL_SUPPORTED_CAPABILITIES=""
# Strip the `thinking:{}` body field entirely. CC's "adaptive" value isn't in z.ai's spec;
# DISABLE_ADAPTIVE_THINKING didn't downgrade it (verified via mitm). Better to send nothing
# and let GLM use its server-side default (thinking ON) than to send an unverified value.
export CLAUDE_CODE_DISABLE_ADAPTIVE_THINKING=1
export CLAUDE_CODE_DISABLE_THINKING=1
# Strip all anthropic-beta: headers (claude-code-*, interleaved-thinking-*, effort-*).
# Relevant for us because z.ai's Anthropic shim doesn't implement Anthropic-internal betas.
export CLAUDE_CODE_SIMULATE_PROXY_USAGE=1

# Compute the memory dir exactly the way native CC does:
# $HOME/.claude/projects/<cwd-with-slashes-replaced-by-dashes>/memory
SLUG="$(echo "$CWD" | sed 's|/|-|g')"           # e.g. /path/to/proj -> -path-to-proj
MEM_DIR="$HOME/.claude/projects/$SLUG/memory"
RENDERED_PROMPT="$CWD/.claude/cc-prompt.md"

mkdir -p "$MEM_DIR" "$(dirname "$RENDERED_PROMPT")"

# Render the prompt template — substitute {{MEMORY_DIR}} with the resolved deep path
sed "s|{{MEMORY_DIR}}|$MEM_DIR|g" "$DIR/claude-code-glm-prompt.md" > "$RENDERED_PROMPT"

exec claude --system-prompt-file "$RENDERED_PROMPT" --disallowed-tools Skill "$@"
