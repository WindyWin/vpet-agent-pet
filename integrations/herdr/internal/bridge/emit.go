package bridge

import (
	"bytes"
	"fmt"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
)

// FindAgentPet locates the agent-pet executable on the host.
func FindAgentPet() string {
	if p := os.Getenv("AGENT_PET_BIN"); p != "" {
		if fi, err := os.Stat(p); err == nil && !fi.IsDir() {
			return p
		}
	}
	if p, err := exec.LookPath("agent-pet"); err == nil {
		return p
	}

	home, _ := os.UserHomeDir()
	candidates := []string{
		filepath.Join(home, ".local", "bin", "agent-pet"),
		"/usr/local/bin/agent-pet",
		"/usr/bin/agent-pet",
	}
	if runtime.GOOS == "darwin" {
		candidates = append(candidates, "/Applications/Agent Pet.app/Contents/MacOS/agent-pet")
	}
	if root := os.Getenv("HERDR_PLUGIN_ROOT"); root != "" {
		candidates = append([]string{filepath.Join(root, "bin", "agent-pet")}, candidates...)
	}

	for _, c := range candidates {
		if fi, err := os.Stat(c); err == nil && !fi.IsDir() {
			return c
		}
	}
	return ""
}

// EventSocketPath returns the standard Unix datagram socket path for Agent Pet.
func EventSocketPath() string {
	uid := os.Getuid()
	if runtime.GOOS == "darwin" {
		tmp := os.Getenv("TMPDIR")
		if tmp == "" {
			tmp = "/tmp"
		}
		return filepath.Join(tmp, fmt.Sprintf("agent-pet-%d", uid), "events.sock")
	}
	runtimeDir := os.Getenv("XDG_RUNTIME_DIR")
	if runtimeDir == "" {
		runtimeDir = fmt.Sprintf("/run/user/%d", uid)
	}
	return filepath.Join(runtimeDir, fmt.Sprintf("agent-pet-%d", uid), "events.sock")
}

// Emit sends a normalized JSON payload to the running Agent Pet instance.
func Emit(payload []byte) error {
	sockPath := EventSocketPath()
	if conn, err := net.Dial("unixgram", sockPath); err == nil {
		defer conn.Close()
		if _, err := conn.Write(payload); err == nil {
			return nil
		}
	}

	// Fallback to calling agent-pet emit
	agentPet := FindAgentPet()
	if agentPet == "" {
		return fmt.Errorf("agent-pet binary not found and socket %s unreachable", sockPath)
	}
	cmd := exec.Command(agentPet, "emit")
	cmd.Stdin = bytes.NewReader(payload)
	return cmd.Run()
}
