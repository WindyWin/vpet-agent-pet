package herdr

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// SocketPath returns the active Herdr API socket path.
func SocketPath() string {
	if p := os.Getenv("HERDR_SOCKET_PATH"); p != "" {
		return filepath.Clean(p)
	}
	configHome := os.Getenv("XDG_CONFIG_HOME")
	if configHome == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return ""
		}
		configHome = filepath.Join(home, ".config")
	}
	session := os.Getenv("HERDR_SESSION")
	if session == "" || session == "default" {
		return filepath.Join(configHome, "herdr", "herdr.sock")
	}
	return filepath.Join(configHome, "herdr", "sessions", session, "herdr.sock")
}

// Call sends a single JSON-RPC request to Herdr socket and reads the response.
func Call(ctx context.Context, socketPath string, method string, params any, out any) error {
	if socketPath == "" {
		return errors.New("herdr socket path is empty")
	}
	conn, err := dialSocket(ctx, socketPath)
	if err != nil {
		return fmt.Errorf("connect to herdr: %w", err)
	}
	defer conn.Close()

	if params == nil {
		params = struct{}{}
	}
	reqData, err := json.Marshal(Request{
		ID:     fmt.Sprintf("r%d", time.Now().UnixNano()),
		Method: method,
		Params: params,
	})
	if err != nil {
		return fmt.Errorf("marshal request: %w", err)
	}

	_ = conn.SetDeadline(time.Now().Add(3 * time.Second))
	if _, err := conn.Write(append(reqData, '\n')); err != nil {
		return fmt.Errorf("write request: %w", err)
	}

	scanner := bufio.NewScanner(conn)
	if !scanner.Scan() {
		if err := scanner.Err(); err != nil {
			return fmt.Errorf("read response: %w", err)
		}
		return errors.New("unexpected EOF reading herdr response")
	}

	var resp Response
	if err := json.Unmarshal(scanner.Bytes(), &resp); err != nil {
		return fmt.Errorf("unmarshal response: %w", err)
	}
	if resp.Error != nil {
		return fmt.Errorf("herdr error %s: %s", resp.Error.Code, resp.Error.Message)
	}

	if out != nil && len(resp.Result) > 0 {
		if err := json.Unmarshal(resp.Result, out); err != nil {
			return fmt.Errorf("unmarshal result: %w", err)
		}
	}
	return nil
}

// ListAgents calls `agent.list` on the Herdr socket.
func ListAgents(ctx context.Context, socketPath string) ([]AgentInfo, error) {
	var res AgentListResult
	if err := Call(ctx, socketPath, "agent.list", nil, &res); err != nil {
		return nil, err
	}
	return res.Agents, nil
}
