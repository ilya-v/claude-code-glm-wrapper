# claude-code-glm-wrapper

A small launcher that runs [Claude Code](https://github.com/anthropics/claude-code) against
[z.ai](https://z.ai)'s Anthropic-compatible endpoint, backed by GLM-5.1.

Unlike simpler wrappers that only set `ANTHROPIC_BASE_URL` + `ANTHROPIC_AUTH_TOKEN`, this one
also strips the request features GLM's shim doesn't implement (extended thinking, adaptive
thinking, Anthropic-only beta headers) and ships a custom system prompt tuned for GLM
rather than Claude.

## Why the extra env vars?

Claude Code's default request body carries features that z.ai's Anthropic-compatible
shim silently ignores or chokes on:

- `thinking: {type: "enabled", budget_tokens: N}` — GLM's shim doesn't honor it.
- `anthropic-beta:` headers (`effort-*`, `interleaved-thinking-*`, `context-1m-*`, …) — Anthropic-internal, not implemented by z.ai.
- Claude-specific capability declarations (`effort`, `adaptive_thinking`, `max_effort`).

The launcher disables all three via `CLAUDE_CODE_DISABLE_THINKING=1`,
`CLAUDE_CODE_SIMULATE_PROXY_USAGE=1`, and empty `*_SUPPORTED_CAPABILITIES` strings.

See [`claude-code-with-glm.md`](./claude-code-with-glm.md) for the full rationale,
each env var explained, and verification steps.

## Why a custom system prompt?

Claude Code's default system prompt is tuned for Claude: aggressive length anchors
(`≤100 words`), "don't narrate your internal deliberation", and heavy reliance on
`<system-reminder>` blocks that are only explained in the Opus-4.7-gated meta-prompt.
Those instructions *suppress* the reasoning chain GLM needs to produce good answers.

[`claude-code-glm-prompt.md`](./claude-code-glm-prompt.md) is a prompt rewritten for GLM:
the Claude-only sections removed, reasoning encouraged instead of discouraged, and
the `{{MEMORY_DIR}}` placeholder substituted at launch time with the auto-memory path
Claude Code would otherwise derive itself.

## Setup

1. Install Claude Code: `npm install -g @anthropic-ai/claude-code`
2. Get a z.ai API key from <https://z.ai> and drop it in a file named `zai.key` next to `cc` (one line, just the key).
3. Make `cc` executable: `chmod +x cc`
4. Run it from any project directory:

   ```bash
   /path/to/claude-code-glm-wrapper/cc
   ```

The launcher creates `./.claude/cc-prompt.md` (rendered system prompt) and
`./.claude/memory/` (auto-memory base) inside whatever directory you run it from.

`zai.key` is gitignored — it will never be committed.

## Files

| File | Purpose |
|---|---|
| `cc` | The launcher script |
| `claude-code-glm-prompt.md` | System prompt template (GLM-tuned, with `{{MEMORY_DIR}}` placeholder) |
| `claude-code-with-glm.md` | Detailed write-up of the env vars, why each is needed, and how to verify |

## Related

- [`alchaincyf/glm-claude`](https://github.com/alchaincyf/glm-claude) — npm-packaged wrapper, simpler approach (just the two core env vars, no thinking/beta-header stripping, no custom prompt).
- [`1rgs/claude-code-proxy`](https://github.com/1rgs/claude-code-proxy) — proxy approach, not env-var-based.
