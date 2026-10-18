package herdr

import "encoding/json"

type Request struct {
	ID     string `json:"id"`
	Method string `json:"method"`
	Params any    `json:"params"`
}

type Response struct {
	ID     string          `json:"id"`
	Result json.RawMessage `json:"result,omitempty"`
	Error  *WireError      `json:"error,omitempty"`
}

type WireError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type EventEnvelope struct {
	Event string          `json:"event"`
	Data  json.RawMessage `json:"data"`
}

type Subscription struct {
	Type   string `json:"type"`
	PaneID string `json:"pane_id,omitempty"`
}

type SubscribeParams struct {
	Subscriptions []Subscription `json:"subscriptions"`
}

type AgentInfo struct {
	PaneID      string `json:"pane_id"`
	WorkspaceID string `json:"workspace_id"`
	TabID       string `json:"tab_id"`
	Agent       string `json:"agent"`
	AgentStatus string `json:"agent_status"`
	Title       string `json:"title"`
	Cwd         string `json:"cwd"`
	Focused     bool   `json:"focused"`
}

type AgentListResult struct {
	Agents []AgentInfo `json:"agents"`
}

type PaneAgentStatusChangedData struct {
	PaneID      string `json:"pane_id"`
	WorkspaceID string `json:"workspace_id"`
	TabID       string `json:"tab_id"`
	Agent       string `json:"agent"`
	AgentStatus string `json:"agent_status"`
	Title       string `json:"title"`
	Cwd         string `json:"cwd"`
	Focused     bool   `json:"focused"`
}

type PaneClosedData struct {
	PaneID      string `json:"pane_id"`
	WorkspaceID string `json:"workspace_id"`
}
