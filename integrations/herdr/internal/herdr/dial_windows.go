//go:build windows

package herdr

import (
	"context"
	"net"
	"time"
)

func dialSocket(ctx context.Context, path string) (net.Conn, error) {
	// Standard pipe dialer
	var d net.Dialer
	d.Timeout = 3 * time.Second
	return d.DialContext(ctx, "unix", path)
}
