package bridge

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"strings"
	"time"

	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/herdr"
)

type PetEvent struct {
	Version     int    `json:"version"`
	Provider    string `json:"provider"`
	SessionID   string `json:"session_id"`
	EventID     string `json:"event_id"`
	Kind        string `json:"kind"`
	TimestampMS int64  `json:"timestamp_ms"`
	Activity    string `json:"activity,omitempty"`
	ToolID      string `json:"tool_id,omitempty"`
	Reason      string `json:"reason,omitempty"`
	ProjectPath string `json:"project_path,omitempty"`
	Host        string `json:"host,omitempty"`
	HostTarget  string `json:"host_target,omitempty"`
}

func randomUUID() string {
	b := make([]byte, 16)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}

func normalizeProvider(agent string) string {
	switch strings.ToLower(agent) {
	case "claude", "claude-code":
		return "claude"
	case "codex":
		return "codex"
	case "opencode":
		return "opencode"
	case "antigravity":
		return "antigravity"
	default:
		return "claude"
	}
}

// TranslateEvent transforms a Herdr event into Agent Pet's normalized v1 JSON payload.
func TranslateEvent(ev herdr.Event, socketPath string) ([]byte, bool) {
	if ev.PaneID == "" {
		return nil, false
	}

	workspace := ev.WorkspaceID
	if workspace == "" && strings.Contains(ev.TabID, ":") {
		workspace = strings.SplitN(ev.TabID, ":", 2)[0]
	} else if workspace == "" && strings.Contains(ev.PaneID, ":") {
		workspace = strings.SplitN(ev.PaneID, ":", 2)[0]
	}

	// 4-part target supported by Agent Pet (#114)
	target := workspace + "|" + ev.TabID + "|" + ev.PaneID + "|" + socketPath

	pe := PetEvent{
		Version:     1,
		Provider:    normalizeProvider(ev.Agent),
		SessionID:   ev.PaneID,
		EventID:     randomUUID(),
		TimestampMS: time.Now().UnixMilli(),
		ProjectPath: ev.Cwd,
		Host:        "herdr",
		HostTarget:  target,
	}

	switch ev.Type {
	case "pane.closed", "pane.exited":
		pe.Kind = "session_end"

	case "pane.agent_detected":
		pe.Kind = "session_start"

	case "pane.agent_status_changed":
		status := strings.ToLower(ev.AgentStatus)
		switch status {
		case "working", "busy", "running":
			pe.Kind = "tool_start"
			pe.ToolID = "herdr-task"
			pe.Activity = "working"
		case "blocked", "waiting", "input", "approval":
			pe.Kind = "attention"
			pe.Reason = "approval"
		case "idle", "done", "finished":
			pe.Kind = "turn_finished"
		case "error", "failed":
			pe.Kind = "turn_failed"
		default:
			pe.Kind = "tool_start"
			pe.ToolID = "herdr-task"
			pe.Activity = "working"
		}

	default:
		return nil, false
	}

	data, err := json.Marshal(pe)
	if err != nil {
		return nil, false
	}
	return data, true
}
