package herdr

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"sync"
	"time"
)

type Event struct {
	Type        string
	PaneID      string
	WorkspaceID string
	TabID       string
	Agent       string
	AgentStatus string
	Title       string
	Cwd         string
	Focused     bool
}

type Stream struct {
	socketPath string
	events     chan Event
	panesMu    sync.Mutex
	panes      map[string]bool
	resubChan  chan struct{}
}

func NewStream(socketPath string) *Stream {
	return &Stream{
		socketPath: socketPath,
		events:     make(chan Event, 64),
		panes:      make(map[string]bool),
		resubChan:  make(chan struct{}, 1),
	}
}

func (s *Stream) Events() <-chan Event {
	return s.events
}

func (s *Stream) WatchPane(paneID string) {
	if paneID == "" {
		return
	}
	s.panesMu.Lock()
	if !s.panes[paneID] {
		s.panes[paneID] = true
		s.panesMu.Unlock()
		select {
		case s.resubChan <- struct{}{}:
		default:
		}
	} else {
		s.panesMu.Unlock()
	}
}

func (s *Stream) UnwatchPane(paneID string) {
	s.panesMu.Lock()
	delete(s.panes, paneID)
	s.panesMu.Unlock()
}

func (s *Stream) Run(ctx context.Context) {
	backoff := 250 * time.Millisecond
	for {
		if ctx.Err() != nil {
			return
		}
		err := s.runConnection(ctx)
		if ctx.Err() != nil {
			return
		}
		if err != nil {
			log.Printf("herdr stream disconnected: %v, retrying in %v...", err, backoff)
		}
		select {
		case <-ctx.Done():
			return
		case <-time.After(backoff):
			backoff *= 2
			if backoff > 5*time.Second {
				backoff = 5 * time.Second
			}
		}
	}
}

func (s *Stream) runConnection(ctx context.Context) error {
	conn, err := dialSocket(ctx, s.socketPath)
	if err != nil {
		return err
	}
	defer conn.Close()

	if err := s.sendSubscribe(conn); err != nil {
		return err
	}

	lines := make(chan []byte, 16)
	errChan := make(chan error, 1)

	go func() {
		scanner := bufio.NewScanner(conn)
		scanner.Buffer(make([]byte, 0, 64*1024), 1024*1024)
		for scanner.Scan() {
			line := append([]byte(nil), scanner.Bytes()...)
			lines <- line
		}
		if err := scanner.Err(); err != nil {
			errChan <- err
		} else {
			errChan <- fmt.Errorf("connection closed")
		}
	}()

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-s.resubChan:
			// Re-subscribe with updated pane list
			if err := s.sendSubscribe(conn); err != nil {
				return err
			}
		case err := <-errChan:
			return err
		case line := <-lines:
			s.handleLine(line)
		}
	}
}

func (s *Stream) sendSubscribe(conn net.Conn) error {
	s.panesMu.Lock()
	subs := []Subscription{
		{Type: "pane.agent_detected"},
		{Type: "pane.closed"},
		{Type: "pane.exited"},
		{Type: "pane.updated"},
	}
	for p := range s.panes {
		subs = append(subs, Subscription{Type: "pane.agent_status_changed", PaneID: p})
	}
	s.panesMu.Unlock()

	req := Request{
		ID:     "sub",
		Method: "events.subscribe",
		Params: SubscribeParams{Subscriptions: subs},
	}
	data, err := json.Marshal(req)
	if err != nil {
		return err
	}
	_ = conn.SetWriteDeadline(time.Now().Add(3 * time.Second))
	_, err = conn.Write(append(data, '\n'))
	_ = conn.SetWriteDeadline(time.Time{})
	return err
}

func (s *Stream) handleLine(line []byte) {
	var env EventEnvelope
	if err := json.Unmarshal(line, &env); err != nil || env.Event == "" {
		return
	}

	switch env.Event {
	case "pane.agent_detected":
		var d struct {
			PaneID      string  `json:"pane_id"`
			WorkspaceID string  `json:"workspace_id"`
			Agent       *string `json:"agent"`
		}
		if err := json.Unmarshal(env.Data, &d); err == nil && d.PaneID != "" {
			s.WatchPane(d.PaneID)
			s.events <- Event{
				Type:        "pane.agent_detected",
				PaneID:      d.PaneID,
				WorkspaceID: d.WorkspaceID,
				Agent:       stringValue(d.Agent),
			}
		}

	case "pane.agent_status_changed":
		var d PaneAgentStatusChangedData
		if err := json.Unmarshal(env.Data, &d); err == nil && d.PaneID != "" {
			s.events <- Event{
				Type:        "pane.agent_status_changed",
				PaneID:      d.PaneID,
				WorkspaceID: d.WorkspaceID,
				TabID:       d.TabID,
				Agent:       d.Agent,
				AgentStatus: d.AgentStatus,
				Title:       d.Title,
				Cwd:         d.Cwd,
				Focused:     d.Focused,
			}
		}

	case "pane.closed", "pane.exited":
		var d PaneClosedData
		if err := json.Unmarshal(env.Data, &d); err == nil && d.PaneID != "" {
			s.UnwatchPane(d.PaneID)
			s.events <- Event{
				Type:        env.Event,
				PaneID:      d.PaneID,
				WorkspaceID: d.WorkspaceID,
			}
		}
	}
}

func stringValue(p *string) string {
	if p == nil {
		return ""
	}
	return *p
}
