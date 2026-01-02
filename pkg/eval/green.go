package eval

import (
	"purple_go/pkg/ast"
)

// Green thread scheduler based on prompt/control delimited continuations.
// No OS threads or Go channels - pure cooperative multitasking.

// GreenScheduler manages green threads using continuations
type GreenScheduler struct {
	runQueue    []func()       // Queue of ready continuations
	inGreenCtx  bool           // Are we inside a green thread context?
	promptTag   int            // Current prompt tag for green threads
}

// Global green scheduler
var greenSched = &GreenScheduler{
	runQueue:   make([]func(), 0),
	inGreenCtx: false,
}

// GetGreenScheduler returns the global green scheduler
func GetGreenScheduler() *GreenScheduler {
	return greenSched
}

// InGreenContext returns true if we're inside a go block
func InGreenContext() bool {
	return greenSched.inGreenCtx
}

// Spawn adds a thunk to the run queue
func (s *GreenScheduler) Spawn(thunk func()) {
	s.runQueue = append(s.runQueue, thunk)
}

// Yield reschedules current green thread (called via control)
func (s *GreenScheduler) Yield(resume func()) {
	s.runQueue = append(s.runQueue, resume)
}

// Run executes all green threads until none remain
func (s *GreenScheduler) Run() {
	s.inGreenCtx = true
	defer func() { s.inGreenCtx = false }()

	for len(s.runQueue) > 0 {
		// Dequeue next task
		task := s.runQueue[0]
		s.runQueue = s.runQueue[1:]

		// Run it (may add more tasks via Yield)
		task()
	}
}

// Reset clears the scheduler state
func (s *GreenScheduler) Reset() {
	s.runQueue = make([]func(), 0)
	s.inGreenCtx = false
}

// GreenChannel is a pure continuation-based channel (no Go channels)
type GreenChannel struct {
	buffer      []*ast.Value           // Buffered values
	capacity    int                    // Max buffer size (0 = unbuffered)
	sendWaiters []sendWaiter           // Blocked senders
	recvWaiters []func(*ast.Value)     // Blocked receivers (continuations)
	closed      bool
}

type sendWaiter struct {
	value  *ast.Value
	resume func()
}

// NewGreenChannel creates a new green channel
func NewGreenChannel(capacity int) *GreenChannel {
	return &GreenChannel{
		buffer:      make([]*ast.Value, 0, capacity),
		capacity:    capacity,
		sendWaiters: make([]sendWaiter, 0),
		recvWaiters: make([]func(*ast.Value), 0),
		closed:      false,
	}
}

// TrySend attempts non-blocking send
// Returns true if sent, false if would block
func (ch *GreenChannel) TrySend(val *ast.Value) bool {
	// If there's a waiting receiver, hand off directly
	if len(ch.recvWaiters) > 0 {
		recv := ch.recvWaiters[0]
		ch.recvWaiters = ch.recvWaiters[1:]
		// Schedule receiver with value
		greenSched.Spawn(func() { recv(val) })
		return true
	}

	// If buffer has space, add to buffer
	if len(ch.buffer) < ch.capacity {
		ch.buffer = append(ch.buffer, val)
		return true
	}

	// Would block
	return false
}

// TryRecv attempts non-blocking receive
// Returns (value, true) if received, (nil, false) if would block
func (ch *GreenChannel) TryRecv() (*ast.Value, bool) {
	// If buffer has data, take from buffer
	if len(ch.buffer) > 0 {
		val := ch.buffer[0]
		ch.buffer = ch.buffer[1:]

		// If sender waiting, move their value to buffer
		if len(ch.sendWaiters) > 0 {
			sw := ch.sendWaiters[0]
			ch.sendWaiters = ch.sendWaiters[1:]
			ch.buffer = append(ch.buffer, sw.value)
			greenSched.Spawn(sw.resume)
		}

		return val, true
	}

	// If there's a waiting sender (unbuffered), hand off directly
	if len(ch.sendWaiters) > 0 {
		sw := ch.sendWaiters[0]
		ch.sendWaiters = ch.sendWaiters[1:]
		greenSched.Spawn(sw.resume)
		return sw.value, true
	}

	// Would block
	return nil, false
}

// AddSendWaiter adds a blocked sender
func (ch *GreenChannel) AddSendWaiter(val *ast.Value, resume func()) {
	ch.sendWaiters = append(ch.sendWaiters, sendWaiter{value: val, resume: resume})
}

// AddRecvWaiter adds a blocked receiver
func (ch *GreenChannel) AddRecvWaiter(resume func(*ast.Value)) {
	ch.recvWaiters = append(ch.recvWaiters, resume)
}

// Close closes the channel
func (ch *GreenChannel) Close() {
	ch.closed = true
	// Wake all waiting receivers with nil
	for _, recv := range ch.recvWaiters {
		greenSched.Spawn(func() { recv(ast.Nil) })
	}
	ch.recvWaiters = nil
}

// IsClosed returns true if channel is closed
func (ch *GreenChannel) IsClosed() bool {
	return ch.closed
}

// greenYieldEscape is used to yield from a green thread
type greenYieldEscape struct {
	resume func()  // Continuation to resume later
}
