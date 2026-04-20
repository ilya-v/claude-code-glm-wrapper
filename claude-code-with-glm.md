# Claude Code with GLM-5.1 — Actionable Setup

Root causes of GLM-5.1 underperformance inside Claude Code (derived from reverse-engineering the `2.1.112` binary):

1. Claude Code ships a ~10 KB Claude-tuned system prompt with aggressive length anchors (`≤100 words`) and "don't narrate your internal deliberation" — these suppress the reasoning chains GLM needs.
2. The request body carries `thinking:{type:"enabled", budget_tokens:N}` by default for any non-claude-3 model — GLM's Anthropic-compat shim usually doesn't honor this schema.
3. The `# System reminders` meta-explanation (function `WS1()`) is gated to Opus 4.7 only, so GLM sees `<system-reminder>` tags without being told how to read them.
4. `ultrathink` only injects a `<system-reminder>` text line — no real reasoning budget changes — which on GLM is noise, not signal.

Apply the fixes in order of impact.

---

## 1. Kill the `thinking:{}` body field

```bash
export CLAUDE_CODE_DISABLE_THINKING=1
# or equivalently
export MAX_THINKING_TOKENS=0
```

## 2. Collapse the bloated Claude-only system prompt

Biggest single win for any non-Claude model. Replaces the full prompt with:
`"You are Claude Code, Anthropic's official CLI for Claude.\nCWD: …\nDate: …"`

```bash
export CLAUDE_CODE_SIMPLE=1
```

## 3. Strip the Anthropic beta headers

GLM's z.ai compat layer doesn't understand Anthropic-specific beta headers or their associated body fields. `CLAUDE_CODE_SIMULATE_PROXY_USAGE` removes them all in one shot — originally a debug flag for testing third-party proxy compatibility, but exactly what non-Anthropic backends need.

```bash
export CLAUDE_CODE_SIMULATE_PROXY_USAGE=1
```

This removes from every outgoing request:
- The entire `betas:[…]` array (drops `effort-2025-11-24`, `interleaved-thinking-2025-05-14`, `context-1m-2025-08-07`, `context-management-2025-06-27`, `task-budgets-2026-03-13`, `skills-2025-10-02`, `redact-thinking-2026-02-12`, `fast-mode-2026-02-01`, `claude-code-20250219`, and ~15 others).
- Proxy-specific body additions from `buildRequestParams()` (extra fields that only work on first-party Anthropic endpoints).

Leaves intact: `model`, `messages`, `system`, `tools`, `thinking`, `max_tokens`, `temperature`, `metadata`. Logs stripped headers when `ANTHROPIC_LOG=debug`.

## 4. Declare GLM's real capabilities

The `ANTHROPIC_CUSTOM_MODEL_OPTION` string must match the model id you use. Default to empty capabilities — add only ones your z.ai proxy verifiably forwards.

```bash
export ANTHROPIC_CUSTOM_MODEL_OPTION=glm-5.1
export ANTHROPIC_CUSTOM_MODEL_OPTION_SUPPORTED_CAPABILITIES=""
```

Enable individual capabilities only after confirming the proxy handles them:

```bash
# If proxy translates thinking:{type:"enabled"} correctly:
export ANTHROPIC_CUSTOM_MODEL_OPTION_SUPPORTED_CAPABILITIES="thinking"

# If proxy also understands the effort body field:
export ANTHROPIC_CUSTOM_MODEL_OPTION_SUPPORTED_CAPABILITIES="thinking,effort"
```

## 5. Clear any persisted effort level

Inside Claude Code:

```
/effort auto
```

And ensure the env var is not set (or is `auto`/`unset`):

```bash
unset CLAUDE_CODE_EFFORT_LEVEL
```

## 6. Stop using `ultrathink`

It only appends a `<system-reminder>` that GLM can't interpret (the explanation block is Opus-4.7-gated). Write plain prompts.

## 7. Sanity-check what's actually on the wire

Before and after any change:

```bash
export ANTHROPIC_LOG=debug
claude -p "hello" 2>&1 | grep -E '"(model|thinking|effort|betas|system)"' | head
```

Verify absent from requests:
- `"thinking": {...}`
- `"effort": "..."`
- `anthropic-beta: effort-2025-11-24`
- `anthropic-beta: interleaved-thinking-2025-05-14`

## 8. Recommended baseline `.zshrc` / `.bashrc` block

```bash
export ANTHROPIC_BASE_URL=https://api.z.ai/api/anthropic   # or the z.ai compat endpoint you use
export ANTHROPIC_AUTH_TOKEN=<your-z.ai-key>
export ANTHROPIC_MODEL=glm-5.1                             # exact id z.ai expects
export ANTHROPIC_CUSTOM_MODEL_OPTION=glm-5.1
export ANTHROPIC_CUSTOM_MODEL_OPTION_SUPPORTED_CAPABILITIES=""
export CLAUDE_CODE_SIMPLE=1
export CLAUDE_CODE_DISABLE_THINKING=1
export CLAUDE_CODE_SIMULATE_PROXY_USAGE=1
```

## 9. If it's still dumb — strip at the proxy

Env vars above don't remove `cache_control` content-block annotations or nested `tool_use` block shapes. If z.ai's shim chokes on those, run a local proxy (mitmproxy + Python addon, or a small Node script) between Claude Code and z.ai that:

- strips `cache_control` from every content block
- translates assistant `tool_use` blocks to whatever format GLM expects
- (beta headers already removed by `CLAUDE_CODE_SIMULATE_PROXY_USAGE=1`)

---

## Expected outcome

The "car wash walk/drive" logic test should stop failing once (1) + (2) are in place. The length anchors (`≤100 words`) and `# Text output` ("don't narrate your internal deliberation") are what suppress the reasoning chain GLM needs to answer correctly.

---

## Reference: env vars that alter Claude Code model channels

| Env var | Effect |
|---|---|
| `CLAUDE_CODE_SIMPLE=1` | Replaces full system prompt with single line |
| `CLAUDE_CODE_SIMULATE_PROXY_USAGE=1` | Strips all `anthropic-beta` headers + proxy-specific body params |
| `CLAUDE_CODE_DISABLE_THINKING=1` | Drops `thinking:{…}` from body |
| `CLAUDE_CODE_DISABLE_ADAPTIVE_THINKING=1` | Downgrades `type:"adaptive"` to `type:"enabled"` |
| `MAX_THINKING_TOKENS=N` | Fixed `budget_tokens: N` (0 = disabled) |
| `CLAUDE_CODE_EFFORT_LEVEL=low\|medium\|high\|xhigh\|max\|auto\|unset` | Overrides settings effort |
| `CLAUDE_CODE_ALWAYS_ENABLE_EFFORT=1` | Forces `Eb()` true (sends `effort` field to any model) |
| `ANTHROPIC_CUSTOM_MODEL_OPTION=<name>` | Marker model name for capability lookup |
| `ANTHROPIC_CUSTOM_MODEL_OPTION_SUPPORTED_CAPABILITIES=thinking,effort,adaptive_thinking,max_effort` | Per-model capability override (read by `Bo()`) |
