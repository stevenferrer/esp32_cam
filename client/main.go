package main

import (
	"bytes"
	"context"
	"log/slog"
	"net"
	"os"
	"os/signal"
	"syscall"
	"time"

	"gocv.io/x/gocv"
)

const (
	listenPort = 31416
	listenIP   = "192.168.4.2"
	streamAddr = "192.168.4.1:9000"
)

var (
	startOfFramePrefix = []byte{0xff, 0xd8, 0xff}
	endOfFrameSuffix   = []byte{0xff, 0xd9}
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stderr, &slog.HandlerOptions{
		Level: slog.LevelError,
	}))

	slog.SetDefault(logger)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	fbChan := make(chan []byte, 1)
	go rcvUdpStream(ctx, fbChan)

	win := gocv.NewWindow("esp32 cam")
	defer win.Close()

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)

	slog.Info("Stream started...")
	showStream(c, win, fbChan)

	slog.Info("Closing stream...")

	close(fbChan)
}

func rcvUdpStream(ctx context.Context, fbChan chan<- []byte) {
	udpConn, err := net.ListenUDP("udp", &net.UDPAddr{
		Port: listenPort,
		IP:   net.ParseIP(listenIP),
	})
	if err != nil {
		slog.Error("net listen", "err", err)
		return
	}
	defer udpConn.Close()

	conn, err := net.Dial("udp", streamAddr)
	if err != nil {
		slog.Error("net dial", "err", err)
		return
	}
	defer conn.Close()

	conn.SetDeadline(time.Now().Add(time.Second))

	slog.Info("Receving stream...")

	fb := make([]byte, 65536)
	for {
		select {
		case <-ctx.Done():
			slog.Info("UDP stream stopped")
			return
		default:
			// continue receiving stream
		}

		buf := make([]byte, 65536)
		_, _, err = udpConn.ReadFromUDP(buf)
		if err != nil {
			if e, ok := err.(net.Error); !ok || !e.Timeout() {
				slog.Error("conn read", "err", err)
				continue
			}

			// send trigger to start stream
			_, err = conn.Write([]byte{})
			if err != nil {
				slog.Error("conn write", "err", err)
			}
			continue
		}

		startOfFrame := bytes.Index(buf, startOfFramePrefix)
		endOfFrame := bytes.LastIndex(buf, endOfFrameSuffix)

		slog.Info("buf info", "len", len(buf), "start", startOfFrame, "end", endOfFrame)

		// start of frame
		if startOfFrame >= 0 {
			// handle start of frame
			if bytes.HasPrefix(fb, startOfFramePrefix) {
				if endOfFrame >= 0 {
					// complete frame
					fb = append(fb, buf[:endOfFrame+2]...)
					endOfFrame = -1
				} else {
					// incomplete frame
					fb = append(fb, buf[:startOfFrame]...)
				}

				// send frame to fbChan
				fbChan <- fb
			}

			// start new frame
			fb = buf[startOfFrame:]

		} else {
			fb = append(fb, buf...)
		}

		if endOfFrame >= 0 {
			endOfByte := len(fb) - len(buf) + endOfFrame + 2
			if bytes.HasPrefix(fb, startOfFramePrefix) {
				// complete frame
				fbChan <- fb[:endOfByte]
			} else {
				// invalid frame
				slog.Info("Invalid picture")
			}

			// remove last frame
			fb = fb[endOfByte:]
		}
	}

}

func showStream(sigChan <-chan os.Signal, win *gocv.Window, fbChan <-chan []byte) {
	for {
		select {
		case fb, ok := <-fbChan:
			if !ok {
				slog.Info("fb chan closed")
				return
			}

			mat, err := gocv.IMDecode(fb, gocv.IMReadColor)
			if err != nil {
				slog.Error("decode image", "err", err)
				return
			}

			win.IMShow(mat)
			if win.PollKey() > 1 {
				return
			}

		case <-sigChan:
			slog.Info("stream stopped")
			return
		}
	}
}
