package doctor

import (
	"context"
	"fmt"
	"io"
	"os"

	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/bridge"
	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/herdr"
)

type CheckResult struct {
	Name    string
	Passed  bool
	Message string
}

func RunDiagnostics(ctx context.Context) []CheckResult {
	var results []CheckResult

	// 1. Display Server Check
	if bridge.HasDisplay() {
		results = append(results, CheckResult{
			Name:    "Graphical Display",
			Passed:  true,
			Message: "Display server active ($DISPLAY or $WAYLAND_DISPLAY)",
		})
	} else {
		results = append(results, CheckResult{
			Name:    "Graphical Display",
			Passed:  false,
			Message: "No graphical session detected; desktop pet requires a local display",
		})
	}

	// 2. Agent Pet Binary Check
	petBin := bridge.FindAgentPet()
	if petBin != "" {
		results = append(results, CheckResult{
			Name:    "Agent Pet Executable",
			Passed:  true,
			Message: fmt.Sprintf("Found at: %s", petBin),
		})
	} else {
		results = append(results, CheckResult{
			Name:    "Agent Pet Executable",
			Passed:  false,
			Message: "agent-pet was not found in PATH or standard install locations",
		})
	}

	// 3. Herdr Socket Check
	sockPath := herdr.SocketPath()
	if sockPath == "" {
		results = append(results, CheckResult{
			Name:    "Herdr Socket",
			Passed:  false,
			Message: "HERDR_SOCKET_PATH is not set and default socket not found",
		})
	} else if _, err := os.Stat(sockPath); err != nil {
		results = append(results, CheckResult{
			Name:    "Herdr Socket",
			Passed:  false,
			Message: fmt.Sprintf("Socket not found at %s: %v", sockPath, err),
		})
	} else {
		agents, err := herdr.ListAgents(ctx, sockPath)
		if err != nil {
			results = append(results, CheckResult{
				Name:    "Herdr Socket",
				Passed:  false,
				Message: fmt.Sprintf("Socket at %s responded with error: %v", sockPath, err),
			})
		} else {
			results = append(results, CheckResult{
				Name:    "Herdr Socket",
				Passed:  true,
				Message: fmt.Sprintf("Connected to %s (%d agents reported)", sockPath, len(agents)),
			})
		}
	}

	// 4. Agent Pet IPC Socket Check
	petSock := bridge.EventSocketPath()
	if _, err := os.Stat(petSock); err == nil {
		results = append(results, CheckResult{
			Name:    "Agent Pet IPC Socket",
			Passed:  true,
			Message: fmt.Sprintf("Active socket at: %s", petSock),
		})
	} else {
		results = append(results, CheckResult{
			Name:    "Agent Pet IPC Socket",
			Passed:  false,
			Message: fmt.Sprintf("Socket not found at %s (pet may not be running)", petSock),
		})
	}

	// 5. Plugin Bridge Daemon Check
	if pid, running := bridge.IsDaemonRunning(); running {
		results = append(results, CheckResult{
			Name:    "Bridge Daemon",
			Passed:  true,
			Message: fmt.Sprintf("Running (PID %d)", pid),
		})
	} else {
		results = append(results, CheckResult{
			Name:    "Bridge Daemon",
			Passed:  false,
			Message: "Daemon is not currently running",
		})
	}

	return results
}

func PrintReport(w io.Writer, results []CheckResult) {
	fmt.Fprintln(w, "=== Agent Pet Herdr Plugin Diagnostics ===")
	fmt.Fprintln(w)
	allPassed := true
	for _, r := range results {
		status := "[\033[32mPASS\033[0m]"
		if !r.Passed {
			status = "[\033[31mFAIL\033[0m]"
			allPassed = false
		}
		fmt.Fprintf(w, "%-7s %-24s: %s\n", status, r.Name, r.Message)
	}
	fmt.Fprintln(w)
	if allPassed {
		fmt.Fprintln(w, "\033[32mAll checks passed! Agent Pet is ready to use with Herdr.\033[0m")
	} else {
		fmt.Fprintln(w, "\033[33mSome checks need attention. See above for details.\033[0m")
	}
}
