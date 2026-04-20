#!/usr/bin/env bash
# Launch Claude Code wired to z.ai's Anthropic-compatible GLM-5.1 endpoint.
#
# Per-launch setup, all under ./.claude/ in the current working directory:
#   ./.claude/cc-prompt.md    — rendered system prompt (template substituted)
#   ./.claude/memory/         — auto-memory base (CC adds projects/<slug>/memory/ underneath)
set -eu
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CWD="$(pwd -P)"

# Locate the z.ai key file: next to this script, or one level up
# (so the same key can be shared with sibling launchers).
if   [ -r "$DIR/zai.key" ];    then KEY_FILE="$DIR/zai.key"
elif [ -r "$DIR/../zai.key" ]; then KEY_FILE="$DIR/../zai.key"
else echo "cc: zai.key not found in $DIR or $DIR/.." >&2; exit 1
fi

export ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic
export ANTHROPIC_AUTH_TOKEN="$(< "$KEY_FILE")"
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

# Compute paths
MEM_BASE="$CWD/.claude/memory"                  # CLAUDE_CODE_REMOTE_MEMORY_DIR base
SLUG="$(echo "$CWD" | sed 's|/|-|g')"           # e.g. /path/to/proj -> -path-to-proj  (CC's slug rule)
MEM_RESOLVED="$MEM_BASE/projects/$SLUG/memory"  # the path CC actually writes/reads
RENDERED_PROMPT="$CWD/.claude/cc-prompt.md"

mkdir -p "$MEM_RESOLVED" "$(dirname "$RENDERED_PROMPT")"

# Render the prompt template — substitute {{MEMORY_DIR}} with the resolved deep path
sed "s|{{MEMORY_DIR}}|$MEM_RESOLVED|g" "$DIR/claude-code-glm-prompt.md" > "$RENDERED_PROMPT"

export CLAUDE_CODE_REMOTE_MEMORY_DIR="$MEM_BASE"
exec claude --system-prompt-file "$RENDERED_PROMPT" --disallowed-tools Skill "$@"
