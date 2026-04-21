// cc-glm-proxy — local reverse proxy in front of z.ai's Anthropic shim.
// Rewrites two request body fields on outgoing /v1/messages POSTs:
//   - temperature      : set to 0.2 (z.ai recommends ~0.2 for deterministic reasoning)
//   - max_tokens       : raise 128000 -> 131072 (CC clamps unknown models to 128000
//                        in Ka(); z.ai documents 131072 as the real GLM-5.1 cap).
// Values smaller than 128000 are left alone (internal CC calls like title
// generation send much smaller caps and shouldn't be bumped).
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
	ccClampCeiling   = 128000                          // value CC's Ka() falls back to for unknown models — the only max_tokens we rewrite
)

func main() {
	port := flag.String("port", defaultPort, "listen port (bound to 127.0.0.1)")
	target := flag.String("target", defaultUpstream, "upstream URL")
	temp := flag.Float64("temperature", defaultTemp, "value to substitute for body.temperature")
	maxTok := flag.Int("max-tokens", defaultMaxTokens, "raise body.max_tokens to this when it equals CC's clamp ceiling")
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
		if newBody, changed := rewrite(body, *temp, *maxTok); changed {
			body = newBody
		}
		r.Body = io.NopCloser(bytes.NewReader(body))
		r.ContentLength = int64(len(body))
		r.Header.Set("Content-Length", strconv.Itoa(len(body)))
	}

	addr := "127.0.0.1:" + *port
	log.Printf("cc-glm-proxy on http://%s -> %s  (temperature=%g, max_tokens-bump=%d)",
		addr, upstream.String(), *temp, *maxTok)
	log.Fatal(http.ListenAndServe(addr, proxy))
}

func rewrite(body []byte, temp float64, maxTok int) ([]byte, bool) {
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
	if !changed {
		return body, false
	}
	out, err := json.Marshal(obj)
	if err != nil {
		return body, false
	}
	log.Printf("rewrote: temperature=%g max_tokens=%v", temp, obj["max_tokens"])
	return out, true
}
