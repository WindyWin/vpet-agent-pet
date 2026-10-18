package bridge

import (
	"encoding/json"
	"testing"

	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/herdr"
)

func TestTranslateEventWorking(t *testing.T) {
	ev := herdr.Event{
		Type:        "pane.agent_status_changed",
		PaneID:      "w1:p1",
		WorkspaceID: "w1",
		TabID:       "w1:t1",
		Agent:       "claude",
		AgentStatus: "working",
		Cwd:         "/work/project",
	}

	data, ok := TranslateEvent(ev, "/tmp/herdr.sock")
	if !ok {
		t.Fatalf("expected TranslateEvent to return ok=true")
	}

	var pe PetEvent
	if err := json.Unmarshal(data, &pe); err != nil {
		t.Fatalf("unmarshal error: %v", err)
	}

	if pe.Version != 1 {
		t.Errorf("expected version 1, got %d", pe.Version)
	}
	if pe.Provider != "claude" {
		t.Errorf("expected provider claude, got %s", pe.Provider)
	}
	if pe.Kind != "tool_start" {
		t.Errorf("expected kind tool_start, got %s", pe.Kind)
	}
	if pe.Activity != "working" {
		t.Errorf("expected activity working, got %s", pe.Activity)
	}
	if pe.Host != "herdr" {
		t.Errorf("expected host herdr, got %s", pe.Host)
	}
	expectedTarget := "w1|w1:t1|w1:p1|/tmp/herdr.sock"
	if pe.HostTarget != expectedTarget {
		t.Errorf("expected host_target %q, got %q", expectedTarget, pe.HostTarget)
	}
}

func TestTranslateEventBlocked(t *testing.T) {
	ev := herdr.Event{
		Type:        "pane.agent_status_changed",
		PaneID:      "p_42",
		WorkspaceID: "w_main",
		TabID:       "t_1",
		Agent:       "codex",
		AgentStatus: "blocked",
		Cwd:         "/work/api",
	}

	data, ok := TranslateEvent(ev, "/run/herdr.sock")
	if !ok {
		t.Fatalf("expected TranslateEvent to return ok=true")
	}

	var pe PetEvent
	if err := json.Unmarshal(data, &pe); err != nil {
		t.Fatalf("unmarshal error: %v", err)
	}

	if pe.Provider != "codex" {
		t.Errorf("expected provider codex, got %s", pe.Provider)
	}
	if pe.Kind != "attention" {
		t.Errorf("expected kind attention, got %s", pe.Kind)
	}
	if pe.Reason != "approval" {
		t.Errorf("expected reason approval, got %s", pe.Reason)
	}
	expectedTarget := "w_main|t_1|p_42|/run/herdr.sock"
	if pe.HostTarget != expectedTarget {
		t.Errorf("expected host_target %q, got %q", expectedTarget, pe.HostTarget)
	}
}

func TestTranslateEventWorkspaceFallback(t *testing.T) {
	// When WorkspaceID is empty, extract from tab_id prefix
	ev := herdr.Event{
		Type:        "pane.agent_status_changed",
		PaneID:      "w2:p5",
		TabID:       "w2:t2",
		Agent:       "opencode",
		AgentStatus: "idle",
		Cwd:         "/work/app",
	}

	data, ok := TranslateEvent(ev, "")
	if !ok {
		t.Fatalf("expected TranslateEvent to return ok=true")
	}

	var pe PetEvent
	if err := json.Unmarshal(data, &pe); err != nil {
		t.Fatalf("unmarshal error: %v", err)
	}

	if pe.Provider != "opencode" {
		t.Errorf("expected provider opencode, got %s", pe.Provider)
	}
	if pe.Kind != "turn_finished" {
		t.Errorf("expected kind turn_finished, got %s", pe.Kind)
	}
	expectedTarget := "w2|w2:t2|w2:p5|"
	if pe.HostTarget != expectedTarget {
		t.Errorf("expected host_target %q, got %q", expectedTarget, pe.HostTarget)
	}
}

func TestTranslateEventClosed(t *testing.T) {
	ev := herdr.Event{
		Type:   "pane.closed",
		PaneID: "w1:p1",
	}

	data, ok := TranslateEvent(ev, "/tmp/sock")
	if !ok {
		t.Fatalf("expected TranslateEvent to return ok=true")
	}

	var pe PetEvent
	if err := json.Unmarshal(data, &pe); err != nil {
		t.Fatalf("unmarshal error: %v", err)
	}

	if pe.Kind != "session_end" {
		t.Errorf("expected kind session_end, got %s", pe.Kind)
	}
}
