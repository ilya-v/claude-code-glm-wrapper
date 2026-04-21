// cc-glm-proxy — local reverse proxy in front of z.ai's Anthropic shim.
// Rewrites fields on outgoing /v1/messages POSTs:
//   - temperature  : substitute with the -temperature flag (CC hardcodes 1).
//   - max_tokens   : substitute when the value equals CC's 128000 clamp
//                    ceiling (the marker for "main request, not internal
//                    title-gen"). Raised or capped depending on the flag.
//   - thinking     : "on" injects {type:"enabled"}; "off" strips the field;
//                    anything else leaves whatever CC sent alone. CC strips
//                    its own thinking block when DISABLE_THINKING=1, so the
//                    only path to server-side reasoning on GLM is to put it
//                    back here.
package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"io"
	"log"
	"net/http"
	"net/http/httputil"
	"net/url"
	"strconv"
	"strings"
)

const (
	defaultPort      = "8765"                          // arbitrary unprivileged port; fixed so parallel cc-glm launches fail visibly rather than silently racing
	defaultUpstream  = "https://api.z.ai/api/anthropic" // only Anthropic-compat endpoint we currently care about
	defaultTemp      = 0.2                             // z.ai's own guidance for deterministic reasoning (docs.z.ai/guides/overview/concept-param)
	defaultMaxTokens = 131072                          // z.ai-documented GLM-5.1 output ceiling
	defaultThinking  = "on"                            // default: force thinking on. Empty / "auto" would leave CC's choice alone.
	ccClampCeiling   = 128000                          // value CC's Ka() falls back to for unknown models — the only max_tokens we rewrite
)

func main() {
	port := flag.String("port", defaultPort, "listen port (bound to 127.0.0.1)")
	target := flag.String("target", defaultUpstream, "upstream URL")
	temp := flag.Float64("temperature", defaultTemp, "value to substitute for body.temperature")
	maxTok := flag.Int("max-tokens", defaultMaxTokens, "raise body.max_tokens to this when it equals CC's clamp ceiling")
	thinking := flag.String("thinking", defaultThinking, `"on" injects {type:"enabled"}; "off" strips the field; anything else leaves CC's choice alone`)
	flag.Parse()

	upstream, err := url.Parse(*target)
	if err != nil {
		log.Fatalf("parse target: %v", err)
	}

	proxy := httputil.NewSingleHostReverseProxy(upstream)
	baseDirector := proxy.Director
	proxy.Director = func(r *http.Request) {
		baseDirector(r)
		r.Host = upstream.Host
		if r.Method != "POST" || !strings.Contains(r.URL.Path, "/v1/messages") || r.Body == nil {
			return
		}
		body, err := io.ReadAll(r.Body)
		if err != nil {
			return
		}
		if newBody, changed := rewrite(body, *temp, *maxTok, *thinking); changed {
			body = newBody
		}
		r.Body = io.NopCloser(bytes.NewReader(body))
		r.ContentLength = int64(len(body))
		r.Header.Set("Content-Length", strconv.Itoa(len(body)))
	}

	addr := "127.0.0.1:" + *port
	log.Printf("cc-glm-proxy on http://%s -> %s  (temperature=%g, max_tokens-bump=%d, thinking=%s)",
		addr, upstream.String(), *temp, *maxTok, *thinking)
	log.Fatal(http.ListenAndServe(addr, proxy))
}

func rewrite(body []byte, temp float64, maxTok int, thinking string) ([]byte, bool) {
	var obj map[string]any
	if err := json.Unmarshal(body, &obj); err != nil {
		return body, false
	}
	changed := false
	// Always override temperature when present — CC hardcodes it to 1
	// and there is no user-facing knob to change that.
	if _, present := obj["temperature"]; present {
		obj["temperature"] = temp
		changed = true
	}
	// Substitute max_tokens only when it's saturated at CC's clamp ceiling —
	// that's how we distinguish the main request from internal CC calls (title
	// generation, model validation) that deliberately ask for short responses.
	// The user-configured value wins whether it's higher or lower than the
	// ceiling; equal is a no-op.
	if cur, ok := obj["max_tokens"].(float64); ok && int(cur) == ccClampCeiling && int(cur) != maxTok {
		obj["max_tokens"] = float64(maxTok)
		changed = true
	}
	// Thinking: "on" forces {type:"enabled"}; "off" removes any thinking field;
	// anything else (empty, "auto", ...) leaves whatever CC sent alone.
	switch strings.ToLower(strings.TrimSpace(thinking)) {
	case "on", "enabled", "true", "1":
		want := map[string]any{"type": "enabled"}
		cur, ok := obj["thinking"].(map[string]any)
		if !ok || cur["type"] != "enabled" || len(cur) != 1 {
			obj["thinking"] = want
			changed = true
		}
	case "off", "disabled", "false", "0":
		if _, has := obj["thinking"]; has {
			delete(obj, "thinking")
			changed = true
		}
	}
	if !changed {
		return body, false
	}
	out, err := json.Marshal(obj)
	if err != nil {
		return body, false
	}
	log.Printf("rewrote: temperature=%g max_tokens=%v thinking=%v", temp, obj["max_tokens"], obj["thinking"])
	return out, true
}
