/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 *
 * Go side of the callback plumbing.  The api gets a C trampoline (cb.c) and
 * a closure that is a runtime/cgo.Handle to the Go registration record; the
 * trampoline calls the exported functions below, which look the record up
 * and call the user's interface. */
package raiapi2

/*
#include <stdint.h>
#include <raiapi2_c.h>

void    *rai_go_handle_ptr( uintptr_t h );
void     rai_go_msg_fn( void *cl,  const rai_msg_event_t *ev,  rai_msg_t msg );
void     rai_go_timer_fn( void *cl,  rai_timer_t timer );
void     rai_go_subscribe_fn( void *cl,  const rai_subscribe_event_t *ev,
                              rai_msg_t msg );
void     rai_go_dataloss_fn( void *cl,  const rai_dataloss_event_t *ev );
void     rai_go_connection_fn( void *cl,  const rai_connection_event_t *ev );
uint32_t rai_go_write_fn( void *cl,  const uint8_t *buf,  uint32_t len );
*/
import "C"

import (
	"io"
	"runtime/cgo"
	"unsafe"
)

/* cbHandle keeps a native callback registration (rai_callback_t) together
 * with the Go objects it needs alive; released after the owner is destroyed */
type cbHandle struct {
	h       cgo.Handle /* handle to this record, the C closure */
	cb      C.rai_callback_t
	target  any /* the user's callback interface */
	closure any /* the user's closure */
	owner   any /* *Subscribe, *Timer, *InteractivePublish, *Session */
}

func newCbHandle(target, closure any) *cbHandle {
	h := &cbHandle{target: target, closure: closure}
	h.h = cgo.NewHandle(h)
	return h
}

func (h *cbHandle) ptr() unsafe.Pointer { return C.rai_go_handle_ptr(C.uintptr_t(h.h)) }

func (h *cbHandle) release() {
	if h == nil {
		return
	}
	if h.cb != nil {
		C.rai_callback_delete(h.cb)
		h.cb = nil
	}
	if h.h != 0 {
		h.h.Delete()
		h.h = 0
	}
}

func cbFrom(cl unsafe.Pointer) *cbHandle {
	return cgo.Handle(uintptr(cl)).Value().(*cbHandle)
}

//export goMsgFn
func goMsgFn(cl unsafe.Pointer, ne *C.rai_msg_event_t, m C.rai_msg_t) {
	h := cbFrom(cl)
	sub := h.owner.(*Subscribe)
	ev := &MsgEvent{
		Subscribe:  sub,
		Subject:    goStr(ne.subject),
		Type:       int(ne._type),
		MsgType:    int16(ne.msg_type),
		RecStatus:  int16(ne.rec_status),
		OldState:   int(ne.old_state),
		Recv:       int(ne.recv),
		State:      int(ne.state),
		PubTime:    int64(ne.pub_time),
		RouteTime:  int64(ne.route_time),
		Counter:    int64(ne.counter),
		OldCounter: int64(ne.old_counter),
	}
	msg := &Msg{msg: m, owned: false} /* owned by the api, valid in callback */
	defer msg.invalidate()
	h.target.(MsgCallback).OnMsg(ev, msg, h.closure)
}

//export goTimerFn
func goTimerFn(cl unsafe.Pointer, _ C.rai_timer_t) {
	h := cbFrom(cl)
	h.target.(TimerCallback).OnTimer(h.owner.(*Timer), h.closure)
}

//export goSubscribeFn
func goSubscribeFn(cl unsafe.Pointer, ne *C.rai_subscribe_event_t, m C.rai_msg_t) {
	h := cbFrom(cl)
	ev := &SubscribeEvent{
		Publish:    h.owner.(*InteractivePublish),
		Subject:    goStr(ne.subject),
		Reply:      goStr(ne.reply),
		QueryFlags: int(ne.query_flags),
	}
	msg := &Msg{msg: m, owned: false}
	defer msg.invalidate()
	h.target.(SubscribeCallback).OnSubscribe(ev, msg, h.closure)
}

//export goDataLossFn
func goDataLossFn(cl unsafe.Pointer, ne *C.rai_dataloss_event_t) {
	h := cbFrom(cl)
	ev := &DataLossEvent{
		Session:            h.owner.(*Session),
		TransportName:      goStr(ne.transport_name),
		Description:        goStr(ne.description),
		InboundPacketLoss:  int64(ne.inbound_packet_loss),
		OutboundPacketLoss: int64(ne.outbound_packet_loss),
		ConnectionCount:    int64(ne.connection_count),
		ConnectionLoss:     ne.connection_loss != 0,
		IsMulticast:        ne.is_multicast != 0,
	}
	h.target.(DataLossCallback).OnDataLoss(ev, h.closure)
}

//export goConnectionFn
func goConnectionFn(cl unsafe.Pointer, ne *C.rai_connection_event_t) {
	h := cbFrom(cl)
	ev := &ConnectionEvent{
		Session:            h.owner.(*Session),
		TransportName:      goStr(ne.transport_name),
		Description:        goStr(ne.description),
		ConnectionCount:    int64(ne.connection_count),
		ConnectionOriented: ne.connection_oriented != 0,
		IsMulticast:        ne.is_multicast != 0,
	}
	h.target.(DataLossCallback).OnConnection(ev, h.closure)
}

/* writer adapter: rai_write_fn -> io.Writer */
type writeAdapter struct {
	h   cgo.Handle
	w   io.Writer
	err error
}

func newWriter(w io.Writer) *writeAdapter {
	a := &writeAdapter{w: w}
	a.h = cgo.NewHandle(a)
	return a
}
func (a *writeAdapter) ptr() unsafe.Pointer { return C.rai_go_handle_ptr(C.uintptr_t(a.h)) }
func (a *writeAdapter) release()            { a.h.Delete() }

//export goWriteFn
func goWriteFn(cl unsafe.Pointer, buf *C.uint8_t, n C.uint32_t) C.uint32_t {
	a := cgo.Handle(uintptr(cl)).Value().(*writeAdapter)
	b := goBytes(unsafe.Pointer(buf), n)
	if _, err := a.w.Write(b); err != nil {
		a.err = err
		return 0 /* broken pipe */
	}
	return n
}
