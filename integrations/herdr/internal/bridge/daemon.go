package bridge

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"syscall"
	"time"

	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/herdr"
)

// HasDisplay checks if a graphical display server is available.
func HasDisplay() bool {
	if runtime.GOOS == "darwin" || runtime.GOOS == "windows" {
		return true
	}
	return os.Getenv("DISPLAY") != "" || os.Getenv("WAYLAND_DISPLAY") != ""
}

// EnsureAgentPetRunning launches the agent-pet GUI if it isn't running.
func EnsureAgentPetRunning() error {
	if !HasDisplay() {
		return fmt.Errorf("no graphical display ($DISPLAY or $WAYLAND_DISPLAY) found")
	}

	agentPetBin := FindAgentPet()
	if agentPetBin == "" {
		return fmt.Errorf("agent-pet binary not found")
	}

	// If events.sock is connectable, agent-pet is already up
	if sock := EventSocketPath(); sock != "" {
		if fi, err := os.Stat(sock); err == nil && !fi.IsDir() {
			return nil
		}
	}

	// Detached launch
	cmd := exec.Command(agentPetBin)
	cmd.Stdin = nil
	cmd.Stdout = nil
	cmd.Stderr = nil
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to start agent-pet: %w", err)
	}

	// Give it up to 2 seconds to initialize socket
	deadline := time.Now().Add(2 * time.Second)
	sockPath := EventSocketPath()
	for time.Now().Before(deadline) {
		if _, err := os.Stat(sockPath); err == nil {
			break
		}
		time.Sleep(100 * time.Millisecond)
	}
	return nil
}

func PIDFilePath() string {
	stateDir := os.Getenv("HERDR_PLUGIN_STATE_DIR")
	if stateDir == "" {
		stateDir = os.TempDir()
	}
	return filepath.Join(stateDir, "herdr-agent-pet.pid")
}

func LogFilePath() string {
	stateDir := os.Getenv("HERDR_PLUGIN_STATE_DIR")
	if stateDir == "" {
		stateDir = os.TempDir()
	}
	return filepath.Join(stateDir, "herdr-agent-pet.log")
}

func IsDaemonRunning() (int, bool) {
	data, err := os.ReadFile(PIDFilePath())
	if err != nil {
		return 0, false
	}
	pid, err := strconv.Atoi(string(data))
	if err != nil || pid <= 0 {
		return 0, false
	}
	proc, err := os.FindProcess(pid)
	if err != nil {
		return 0, false
	}
	// On Unix, sending signal 0 tests process existence
	if err := proc.Signal(syscall.Signal(0)); err != nil {
		return 0, false
	}
	return pid, true
}

func StopDaemon() error {
	pid, running := IsDaemonRunning()
	if !running {
		_ = os.Remove(PIDFilePath())
		return nil
	}
	proc, err := os.FindProcess(pid)
	if err != nil {
		return err
	}
	_ = proc.Signal(syscall.SIGTERM)
	_ = os.Remove(PIDFilePath())
	return nil
}

func RunDaemon(ctx context.Context) error {
	_ = os.WriteFile(PIDFilePath(), []byte(strconv.Itoa(os.Getpid())), 0644)
	defer os.Remove(PIDFilePath())

	socketPath := herdr.SocketPath()
	if socketPath == "" {
		return fmt.Errorf("herdr socket path not found")
	}

	stream := herdr.NewStream(socketPath)

	// Fetch initial agents to watch
	if agents, err := herdr.ListAgents(ctx, socketPath); err == nil {
		for _, a := range agents {
			stream.WatchPane(a.PaneID)
		}
	}

	go stream.Run(ctx)

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case ev := <-stream.Events():
			if payload, ok := TranslateEvent(ev, socketPath); ok {
				if err := Emit(payload); err != nil {
					log.Printf("failed to emit event to agent-pet: %v", err)
				}
			}
		}
	}
}
