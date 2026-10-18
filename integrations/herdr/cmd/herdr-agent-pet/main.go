package main

import (
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"syscall"
	"time"

	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/bridge"
	"github.com/WindyWin/vpet-agent-pet/integrations/herdr/internal/doctor"
)

var version = "0.17.1"

func main() {
	if len(os.Args) < 2 {
		printUsage()
		os.Exit(1)
	}

	cmd := os.Args[1]
	switch cmd {
	case "startup":
		handleStartup()
	case "start":
		handleStart()
	case "stop":
		handleStop()
	case "status":
		handleStatus()
	case "doctor":
		handleDoctor(os.Stdout)
	case "doctor-pane":
		handleDoctorPane()
	case "logs-pane":
		handleLogsPane()
	case "daemon-run":
		handleDaemonRun()
	case "--version", "-v", "version":
		fmt.Printf("herdr-agent-pet version %s\n", version)
	default:
		printUsage()
		os.Exit(1)
	}
}

func printUsage() {
	fmt.Printf("herdr-agent-pet %s - Herdr plugin bridge for Agent Pet\n\n", version)
	fmt.Println("Usage: herdr-agent-pet <command>")
	fmt.Println("\nCommands:")
	fmt.Println("  startup      Session restore entrypoint (launched by Herdr)")
	fmt.Println("  start        Launch Agent Pet and start event bridge")
	fmt.Println("  stop         Stop event bridge")
	fmt.Println("  status       Check connection and process status")
	fmt.Println("  doctor       Run diagnostic checks")
	fmt.Println("  doctor-pane  Diagnostic UI for Herdr pane overlay")
	fmt.Println("  logs-pane    Show daemon log tail")
}

func handleStartup() {
	if !bridge.HasDisplay() {
		log.Println("herdr-agent-pet: headless environment detected, skipping desktop pet GUI")
		return
	}
	if err := bridge.EnsureAgentPetRunning(); err != nil {
		log.Printf("herdr-agent-pet: %v", err)
	}
	startDaemonIfNeeded()
}

func handleStart() {
	if err := bridge.EnsureAgentPetRunning(); err != nil {
		fmt.Fprintf(os.Stderr, "Error starting agent-pet: %v\n", err)
	} else {
		fmt.Println("Agent Pet desktop application is active.")
	}
	if pid, running := bridge.IsDaemonRunning(); running {
		fmt.Printf("Bridge daemon is already running (PID %d).\n", pid)
		return
	}
	startDaemonIfNeeded()
	fmt.Println("Bridge daemon started.")
}

func handleStop() {
	if err := bridge.StopDaemon(); err != nil {
		fmt.Fprintf(os.Stderr, "Failed to stop daemon: %v\n", err)
		os.Exit(1)
	}
	fmt.Println("Bridge daemon stopped.")
}

func handleStatus() {
	fmt.Printf("Agent Pet Herdr Plugin v%s\n", version)
	if bridge.HasDisplay() {
		fmt.Println("Display: Available")
	} else {
		fmt.Println("Display: None (Headless)")
	}

	petBin := bridge.FindAgentPet()
	if petBin != "" {
		fmt.Printf("Agent Pet Binary: %s\n", petBin)
	} else {
		fmt.Println("Agent Pet Binary: Not found")
	}

	if pid, running := bridge.IsDaemonRunning(); running {
		fmt.Printf("Bridge Daemon: Running (PID %d)\n", pid)
	} else {
		fmt.Println("Bridge Daemon: Stopped")
	}
}

func handleDoctor(w io.Writer) {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	results := doctor.RunDiagnostics(ctx)
	doctor.PrintReport(w, results)
}

func handleDoctorPane() {
	handleDoctor(os.Stdout)
	fmt.Println("\nPress Enter or close this pane to dismiss...")
	var b [1]byte
	_, _ = os.Stdin.Read(b[:])
}

func handleLogsPane() {
	logFile := bridge.LogFilePath()
	if _, err := os.Stat(logFile); err != nil {
		fmt.Printf("No log file found at %s yet.\n", logFile)
		return
	}
	cmd := exec.Command("tail", "-n", "100", "-f", logFile)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	_ = cmd.Run()
}

func startDaemonIfNeeded() {
	if _, running := bridge.IsDaemonRunning(); running {
		return
	}
	exe, err := os.Executable()
	if err != nil {
		log.Printf("failed to get executable path: %v", err)
		return
	}
	logFile, err := os.OpenFile(bridge.LogFilePath(), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		log.Printf("failed to open daemon log: %v", err)
		return
	}

	cmd := exec.Command(exe, "daemon-run")
	cmd.Stdout = logFile
	cmd.Stderr = logFile
	cmd.SysProcAttr = &syscall.SysProcAttr{
		Setpgid: true,
	}
	if err := cmd.Start(); err != nil {
		log.Printf("failed to spawn daemon: %v", err)
	}
}

func handleDaemonRun() {
	ctx, cancel := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer cancel()

	log.Printf("herdr-agent-pet daemon started (PID %d)", os.Getpid())
	if err := bridge.RunDaemon(ctx); err != nil && err != context.Canceled {
		log.Printf("herdr-agent-pet daemon stopped with error: %v", err)
	} else {
		log.Printf("herdr-agent-pet daemon stopped cleanly")
	}
}
