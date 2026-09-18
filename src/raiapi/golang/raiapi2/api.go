/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Mirrors com.rai.raiapi2: RaiApi, RaiSession, RaiQueue, RaiSubscribe,
 * RaiPublish, RaiInteractivePublish, RaiTimer, RaiDict, RaiEntitlement,
 * the event types and the callback interfaces. */
package raiapi2

/*
#include <stdlib.h>
#include <raiapi2_c.h>

void     rai_go_msg_fn( void *cl,  const rai_msg_event_t *ev,  rai_msg_t msg );
void     rai_go_timer_fn( void *cl,  rai_timer_t timer );
void     rai_go_subscribe_fn( void *cl,  const rai_subscribe_event_t *ev,
                              rai_msg_t msg );
void     rai_go_dataloss_fn( void *cl,  const rai_dataloss_event_t *ev );
void     rai_go_connection_fn( void *cl,  const rai_connection_event_t *ev );
*/
import "C"

import (
	"os"
	"os/signal"
	"sync"
	"syscall"
	"unsafe"
)

/* ---- callback interfaces ------------------------------------------------ */

type MsgCallback interface {
	OnMsg(ev *MsgEvent, msg *Msg, closure any)
}
type TimerCallback interface {
	OnTimer(timer *Timer, closure any)
}
type SubscribeCallback interface {
	OnSubscribe(ev *SubscribeEvent, msg *Msg, closure any)
}
type DataLossCallback interface {
	OnDataLoss(ev *DataLossEvent, closure any)
	OnConnection(ev *ConnectionEvent, closure any)
}

/* ---- events ------------------------------------------------------------- */

const (
	EVENT_SNAP   = 0 /* MsgEvent.Type */
	EVENT_UPDATE = 1
)

type MsgEvent struct {
	Subscribe  *Subscribe
	Subject    string /* received subject, may be an _INBOX */
	Type       int    /* EVENT_SNAP or EVENT_UPDATE */
	MsgType    int16
	RecStatus  int16
	OldState   int /* STATE_* */
	Recv       int
	State      int
	PubTime    int64 /* ns, 0 if none */
	RouteTime  int64
	Counter    int64
	OldCounter int64
}

func (ev *MsgEvent) SubscribedSubject() string { return ev.Subscribe.Subject() }

type SubscribeEvent struct {
	Publish    *InteractivePublish
	Subject    string
	Reply      string
	QueryFlags int
}
type ConnectionEvent struct {
	Session            *Session
	TransportName      string
	Description        string
	ConnectionCount    int64
	ConnectionOriented bool
	IsMulticast        bool
}
type DataLossEvent struct {
	Session            *Session
	TransportName      string
	Description        string
	InboundPacketLoss  int64
	OutboundPacketLoss int64
	ConnectionCount    int64
	ConnectionLoss     bool
	IsMulticast        bool
}

/* ---- Api ---------------------------------------------------------------- */

const (
	API_ARG          = "api"
	USERID_ARG       = "userid"
	APPID_ARG        = "appid"
	CFILE_PATH_ARG   = "cfilePath"
	TSS_RECORDS_ARG  = "tssRecords"
	TSS_FIELDS_ARG   = "tssFields"
	APPENDIX_A_ARG   = "appendixA"
	ENUMTYPE_DEF_ARG = "enumtypeDef"

	LVL_DEVEL  = 0
	LVL_FTRACE = 1
	LVL_TRACE  = 2
	LVL_DEBUG  = 3
	LVL_MINOR  = 4
	LVL_NORMAL = 5
	LVL_ERROR  = 6

	SIGHUP  = 1
	SIGINT  = 2
	SIGTERM = 15
)

/* Api is a handle to an underlying transport implementation, as well as
 * logging and dictionary management.  Call RaiOpen() to begin. */
type Api struct {
	api C.rai_api_t
}

/* bindingVersion is stamped by the GNUmakefile (-ldflags -X); "" when built
 * with plain go build */
var bindingVersion string

/* RaiOpen opens the api.  apiName selects the transport module ("tibrv", ...
 * or "" for the default / -api argument).  argv is the program's argument
 * list without the program name (os.Args[1:]). */
func RaiOpen(apiName string, argv []string) (*Api, error) {
	av, n, free := cargv(argv)
	defer free()
	cn := cstrOpt(apiName)
	defer cfree(cn)
	var a C.rai_api_t
	if err := check(C.rai_api_open(cn, n, av, &a)); err != nil {
		return nil, err
	}
	return &Api{api: a}, nil
}

/* Delete finalizes the api handle (Close first) */
func (a *Api) Delete() {
	if a.api != nil {
		C.rai_api_delete(a.api)
		a.api = nil
	}
}
func (a *Api) GetApiName() string { return goStr(C.rai_api_name(a.api)) }

/* RaiVersion is the version of the native api, e.g. "1.1.0-2 (f06c315a)" */
func RaiVersion() string { return goStr(C.rai_api_version()) }

/* BindingVersion is the version stamped into the Go package at build time
 * (same source of truth as RaiVersion(): the makefile passes it to go build),
 * or RaiVersion() when not stamped */
func BindingVersion() string {
	if bindingVersion != "" {
		return bindingVersion
	}
	return RaiVersion()
}

func (a *Api) GetArgs(args *Args) error { return check(C.rai_api_get_args(a.api, args.args)) }
func GetDictArgs(args *Args) error      { return check(C.rai_api_get_dict_args(args.args)) }
func (a *Api) ParseArgs(args *Args) error {
	return check(C.rai_api_parse_args(a.api, args.args))
}

/* OpenLogArgs opens the log from -log/-logLevel/-logVerb args, false if not
 * present */
func OpenLogArgs(args *Args) (bool, error) {
	var o C.int
	err := check(C.rai_api_open_log_args(args.args, &o))
	return o != 0, err
}

/* OpenLog opens the log file, "-" is stderr */
func OpenLog(name string, logLevel, logVerb int) error {
	cn := cstr(name)
	defer cfree(cn)
	return check(C.rai_api_open_log(cn, C.int(logLevel), C.int(logVerb)))
}

/* PrintLog logs s at level; err may be nil.  An api error logs its record,
 * any other error is prefixed as text (as the Java binding does) */
func (a *Api) PrintLog(level int, err error, s string) {
	e, s := errOf(err, s)
	cs := cstr(s)
	defer cfree(cs)
	C.rai_api_print_log(a.api, C.int(level), e, nil, 0, cs)
}

/* Log is the api-less log, same levels */
func Log(level int, err error, s string) {
	e, s := errOf(err, s)
	cs := cstr(s)
	defer cfree(cs)
	C.rai_api_log(C.int(level), e, nil, 0, cs)
}

func errOf(err error, s string) (C.rai_err_t, string) {
	if err == nil {
		return nil, s
	}
	if e := nativeErr(err); e != nil {
		return e, s
	}
	return nil, err.Error() + "; " + s
}

/* OpenDict loads the dictionary from the local filesystem if -cfilePath etc
 * are set; true when loaded */
func OpenDict(args *Args) (bool, error) {
	var l C.int
	err := check(C.rai_api_open_dict(args.args, &l))
	return l != 0, err
}

func (a *Api) CreateSession() (*Session, error) {
	var s C.rai_session_t
	if err := check(C.rai_api_create_session(a.api, &s)); err != nil {
		return nil, err
	}
	return &Session{session: s, api: a}, nil
}

/* NewSASSMsg / NewRaiMsg create a message with a SASS header (MSG_TYPE,
 * REC_TYPE or form name, SEQ_NO, REC_STATUS) */
func NewSASSMsg(msgType, recType, seqNo, recStatus int16) (*Msg, error) {
	return NewMsgProto(TIB_SASS_PROTO, msgType, recType, seqNo, recStatus)
}
func NewSASSMsgForm(msgType int16, formType string, seqNo, recStatus int16) (*Msg, error) {
	return NewMsgProtoForm(TIB_SASS_PROTO, msgType, formType, seqNo, recStatus)
}
func NewRaiMsg(msgType, recType, seqNo, recStatus int16) (*Msg, error) {
	return NewMsgProto(RAIMSG_PROTO, msgType, recType, seqNo, recStatus)
}
func NewRaiMsgForm(msgType int16, formType string, seqNo, recStatus int16) (*Msg, error) {
	return NewMsgProtoForm(RAIMSG_PROTO, msgType, formType, seqNo, recStatus)
}
func NewMsgProto(proto int, msgType, recType, seqNo, recStatus int16) (*Msg, error) {
	var m C.rai_msg_t
	if err := check(C.rai_api_new_msg(C.int(proto), C.uint16_t(msgType),
		C.uint16_t(recType), C.uint16_t(seqNo), C.uint16_t(recStatus), &m)); err != nil {
		return nil, err
	}
	return &Msg{msg: m, owned: true}, nil
}
func NewMsgProtoForm(proto int, msgType int16, formType string, seqNo, recStatus int16) (*Msg, error) {
	cf := cstrOpt(formType)
	defer cfree(cf)
	var m C.rai_msg_t
	if err := check(C.rai_api_new_msg_form(C.int(proto), C.uint16_t(msgType), cf,
		C.uint16_t(seqNo), C.uint16_t(recStatus), &m)); err != nil {
		return nil, err
	}
	return &Msg{msg: m, owned: true}, nil
}

/* Close the api; close any open sessions first */
func (a *Api) Close() { C.rai_api_close(a.api) }

func (a *Api) SetIoctl(parameter, value string) bool {
	cp, cv := cstr(parameter), cstr(value)
	defer cfree(cp)
	defer cfree(cv)
	return C.rai_api_set_ioctl(a.api, cp, cv) != 0
}

/* RegisterSigHandler traps SIGINT, SIGHUP, SIGTERM and calls handler(sig).
 * Unlike the C/.NET bindings this is done with os/signal, so the handler runs
 * on a goroutine, not in signal context: it may lock, but it should still
 * only set a flag and let the main flow shut down. */
var (
	sigOnce    sync.Once
	sigMu      sync.Mutex
	sigHandler func(int)
)

func RegisterSigHandler(handler func(sig int)) {
	sigMu.Lock()
	sigHandler = handler
	sigMu.Unlock()
	sigOnce.Do(func() {
		ch := make(chan os.Signal, 4)
		signal.Notify(ch, syscall.SIGINT, syscall.SIGHUP, syscall.SIGTERM)
		go func() {
			for s := range ch {
				sigMu.Lock()
				h := sigHandler
				sigMu.Unlock()
				if h != nil {
					h(int(s.(syscall.Signal)))
				}
			}
		}()
	})
}

/* ---- Session ------------------------------------------------------------ */

/* Session is a connection to the network / service */
type Session struct {
	session    C.rai_session_t
	api        *Api
	dataLossCb *cbHandle
}

func (s *Session) Start() error { return check(C.rai_session_start(s.session)) }

/* CreateQueue; direct = true dispatches messages directly from the recv
 * threads instead of serializing them on the queue thread */
func (s *Session) CreateQueue(direct bool) (*Queue, error) {
	var q C.rai_queue_t
	if err := check(C.rai_session_create_queue(s.session, b2i(direct), &q)); err != nil {
		return nil, err
	}
	return &Queue{queue: q, session: s,
		subs: map[C.rai_subscribe_t]*Subscribe{}, timers: map[C.rai_timer_t]*Timer{},
		ipubs: map[C.rai_ipublish_t]*InteractivePublish{}}, nil
}
func (s *Session) CreatePublish(autoInc bool) (*Publish, error) {
	var p C.rai_publish_t
	if err := check(C.rai_session_create_publish(s.session, b2i(autoInc), &p)); err != nil {
		return nil, err
	}
	return &Publish{publish: p, session: s}, nil
}
func (s *Session) CreateDict() (*Dict, error) {
	var d C.rai_dict_t
	if err := check(C.rai_session_create_dict(s.session, &d)); err != nil {
		return nil, err
	}
	return &Dict{dict: d, session: s}, nil
}
func (s *Session) Destroy() error {
	err := check(C.rai_session_destroy(s.session))
	s.dataLossCb.release()
	s.dataLossCb = nil
	return err
}
func (s *Session) Login(user string) (*Entitlement, error) {
	cu := cstr(user)
	defer cfree(cu)
	var e C.rai_entitle_t
	if err := check(C.rai_session_login(s.session, cu, &e)); err != nil {
		return nil, err
	}
	return &Entitlement{entitle: e}, nil
}
func (s *Session) SetDataLossCB(cb DataLossCallback, closure any) error {
	h := newCbHandle(cb, closure)
	h.owner = s
	var ncb C.rai_callback_t
	if err := check(C.rai_session_set_dataloss_cb(s.session,
		C.rai_dataloss_fn(C.rai_go_dataloss_fn),
		C.rai_connection_fn(C.rai_go_connection_fn), h.ptr(), &ncb)); err != nil {
		h.release()
		return err
	}
	h.cb = ncb
	s.dataLossCb.release()
	s.dataLossCb = h
	return nil
}
func (s *Session) NotifyStatus(msgType, recStatus int16) error {
	return check(C.rai_session_notify_status(s.session, C.uint16_t(msgType), C.uint16_t(recStatus)))
}
func (s *Session) GetApi() *Api { return s.api }
func (s *Session) SetSessionName(name string) error {
	cn := cstr(name)
	defer cfree(cn)
	return check(C.rai_session_set_name(s.session, cn))
}
func (s *Session) GetSessionName() string { return goStr(C.rai_session_get_name(s.session)) }

/* ---- Queue -------------------------------------------------------------- */

/* Queue serializes message and timer events for dispatch */
type Queue struct {
	queue   C.rai_queue_t
	session *Session
	mu      sync.Mutex
	subs    map[C.rai_subscribe_t]*Subscribe
	timers  map[C.rai_timer_t]*Timer
	ipubs   map[C.rai_ipublish_t]*InteractivePublish
}

func (q *Queue) CreateSubscribe(cb MsgCallback, closure any) (*Subscribe, error) {
	h := newCbHandle(cb, closure)
	var s C.rai_subscribe_t
	var ncb C.rai_callback_t
	if err := check(C.rai_queue_create_subscribe(q.queue, C.rai_msg_fn(C.rai_go_msg_fn),
		h.ptr(), &s, &ncb)); err != nil {
		h.release()
		return nil, err
	}
	h.cb = ncb
	sub := &Subscribe{subscribe: s, cb: h, queue: q}
	h.owner = sub
	q.mu.Lock()
	q.subs[s] = sub
	q.mu.Unlock()
	return sub, nil
}
func (q *Queue) CreateTimer(cb TimerCallback, closure any) (*Timer, error) {
	h := newCbHandle(cb, closure)
	var t C.rai_timer_t
	var ncb C.rai_callback_t
	if err := check(C.rai_queue_create_timer(q.queue, C.rai_timer_fn(C.rai_go_timer_fn),
		h.ptr(), &t, &ncb)); err != nil {
		h.release()
		return nil, err
	}
	h.cb = ncb
	timer := &Timer{timer: t, cb: h, queue: q}
	h.owner = timer
	q.mu.Lock()
	q.timers[t] = timer
	q.mu.Unlock()
	return timer, nil
}
func (q *Queue) CreateInteractivePublish(cb SubscribeCallback, closure any) (*InteractivePublish, error) {
	h := newCbHandle(cb, closure)
	var p C.rai_ipublish_t
	var ncb C.rai_callback_t
	if err := check(C.rai_queue_create_ipublish(q.queue, C.rai_subscribe_fn(C.rai_go_subscribe_fn),
		h.ptr(), &p, &ncb)); err != nil {
		h.release()
		return nil, err
	}
	h.cb = ncb
	ip := &InteractivePublish{Publish: Publish{publish: C.rai_ipublish_publish(p), session: q.session},
		interactive: p, cb: h, queue: q}
	h.owner = ip
	q.mu.Lock()
	q.ipubs[p] = ip
	q.mu.Unlock()
	return ip, nil
}
func (q *Queue) NotifyStatus(msgType, recStatus int16) error {
	return check(C.rai_queue_notify_status(q.queue, C.uint16_t(msgType), C.uint16_t(recStatus)))
}

/* Mainloop dispatches events until the queue is destroyed */
func (q *Queue) Mainloop() error { return check(C.rai_queue_mainloop(q.queue)) }

/* TimedDispatch dispatches events for up to ivalMSecs */
func (q *Queue) TimedDispatch(ivalMSecs int) error {
	return check(C.rai_queue_timed_dispatch(q.queue, C.uint32_t(ivalMSecs)))
}

/* Dispatch pending events, return immediately */
func (q *Queue) Dispatch() error { return check(C.rai_queue_dispatch(q.queue)) }
func (q *Queue) GetDepth() int   { return int(C.rai_queue_get_depth(q.queue)) }
func (q *Queue) Destroy() error {
	err := check(C.rai_queue_destroy(q.queue))
	q.mu.Lock()
	defer q.mu.Unlock()
	for _, s := range q.subs {
		s.releaseCB()
	}
	for _, t := range q.timers {
		t.releaseCB()
	}
	for _, p := range q.ipubs {
		p.releaseCB()
	}
	q.subs, q.timers, q.ipubs = map[C.rai_subscribe_t]*Subscribe{},
		map[C.rai_timer_t]*Timer{}, map[C.rai_ipublish_t]*InteractivePublish{}
	return err
}
func (q *Queue) GetSession() *Session { return q.session }

/* ---- Subscribe ---------------------------------------------------------- */

const (
	SUB_UPDATE    = 1 /* Subscribe.Start parm */
	SUB_SNAP      = 2
	SUB_BOTH      = 3
	SUB_NO_PREFIX = 4
	SUB_NO_COPY   = 8

	STATE_NO_MSG   = 0
	STATE_WILDCARD = 1
	STATE_NO_HDR   = 2
	STATE_INITIAL  = 3
	STATE_UPDATE   = 4
	STATE_NOTFOUND = 5
	STATE_STALE    = 6
	STATE_DROPPED  = 7
)

/* Subscribe is a subscription to a subject, created on a queue */
type Subscribe struct {
	subscribe C.rai_subscribe_t
	cb        *cbHandle
	queue     *Queue
	State     int
}

func (s *Subscribe) releaseCB() { s.cb.release(); s.cb = nil }

func StateToString(state int) string        { return goStr(C.rai_subscribe_state_to_string(C.int(state))) }
func (s *Subscribe) GetStateString() string { return StateToString(s.State) }

/* Start the subscription, parm is SUB_UPDATE / SUB_SNAP / SUB_BOTH |
 * SUB_NO_PREFIX | SUB_NO_COPY, timeoutMSecs > 0 generates a STATUS_TIMEOUT if
 * no message arrives */
func (s *Subscribe) Start(subject string, parm int, timeoutMSecs int) error {
	cs := cstr(subject)
	defer cfree(cs)
	return check(C.rai_subscribe_start(s.subscribe, cs, C.int(parm), C.uint32_t(timeoutMSecs)))
}

/* Cancel the subscription; the object is released, do not use it after */
func (s *Subscribe) Cancel() error {
	err := check(C.rai_subscribe_cancel(s.subscribe))
	s.queue.mu.Lock()
	delete(s.queue.subs, s.subscribe)
	s.queue.mu.Unlock()
	s.releaseCB()
	return err
}
func (s *Subscribe) Refresh(timeoutMSecs int) error {
	return check(C.rai_subscribe_refresh(s.subscribe, C.uint32_t(timeoutMSecs)))
}
func (s *Subscribe) Subject() string  { return goStr(C.rai_subscribe_subject(s.subscribe)) }
func (s *Subscribe) InProgress() bool { return C.rai_subscribe_in_progress(s.subscribe) != 0 }
func (s *Subscribe) GetQueue() *Queue { return s.queue }

/* ---- Publish ------------------------------------------------------------ */

/* Publish publishes messages to subjects */
type Publish struct {
	publish C.rai_publish_t
	session *Session
}

/* PublishMsg publishes a message, stamp is a ns timestamp or 0 */
func (p *Publish) PublishMsg(subject string, m *Msg, stamp int64) error {
	cs := cstr(subject)
	defer cfree(cs)
	return check(C.rai_publish_msg(p.publish, cs, m.msg, C.int64_t(stamp)))
}

/* PublishBuf publishes a packed message buffer */
func (p *Publish) PublishBuf(subject string, buf []byte, stamp int64) error {
	cs := cstr(subject)
	defer cfree(cs)
	var ptr unsafe.Pointer
	if len(buf) > 0 {
		ptr = unsafe.Pointer(&buf[0])
	}
	return check(C.rai_publish_buf(p.publish, cs, ptr, C.uint32_t(len(buf)), C.int64_t(stamp)))
}
func (p *Publish) SetPrefix(prefix string) error {
	cp := cstrOpt(prefix)
	defer cfree(cp)
	return check(C.rai_publish_set_prefix(p.publish, cp))
}
func (p *Publish) GetPrefix() string    { return goStr(C.rai_publish_get_prefix(p.publish)) }
func (p *Publish) GetSeqno() int64      { return int64(C.rai_publish_get_seqno(p.publish)) }
func (p *Publish) SetSeqno(n int64)     { C.rai_publish_set_seqno(p.publish, C.uint32_t(n)) }
func (p *Publish) Destroy() error       { return check(C.rai_publish_destroy(p.publish)) }
func (p *Publish) GetSession() *Session { return p.session }

/* InteractivePublish is a publisher that is told when subscriptions start /
 * stop */
type InteractivePublish struct {
	Publish
	interactive C.rai_ipublish_t
	cb          *cbHandle
	queue       *Queue
}

func (p *InteractivePublish) releaseCB() { p.cb.release(); p.cb = nil }
func (p *InteractivePublish) InteractiveStart(subject string) error {
	cs := cstr(subject)
	defer cfree(cs)
	return check(C.rai_ipublish_start(p.interactive, cs))
}
func (p *InteractivePublish) InteractiveCancel() error {
	err := check(C.rai_ipublish_cancel(p.interactive))
	p.queue.mu.Lock()
	delete(p.queue.ipubs, p.interactive)
	p.queue.mu.Unlock()
	p.releaseCB()
	return err
}
func (p *InteractivePublish) InProgress() bool { return C.rai_ipublish_in_progress(p.interactive) != 0 }
func (p *InteractivePublish) GetQueue() *Queue { return p.queue }

/* ---- Timer -------------------------------------------------------------- */

/* Timer is dispatched on a queue */
type Timer struct {
	timer C.rai_timer_t
	cb    *cbHandle
	queue *Queue
}

func (t *Timer) releaseCB()         { t.cb.release(); t.cb = nil }
func (t *Timer) Start() error       { return check(C.rai_timer_start(t.timer)) }
func (t *Timer) Stop()              { C.rai_timer_stop(t.timer) }
func (t *Timer) GetInterval() int64 { return int64(C.rai_timer_get_interval(t.timer)) }
func (t *Timer) SetInterval(intervalMSecs int64) {
	C.rai_timer_set_interval(t.timer, C.int64_t(intervalMSecs))
}
func (t *Timer) GetQueue() *Queue { return t.queue }

/* ---- Dict --------------------------------------------------------------- */

/* Dict is the dictionary loader */
type Dict struct {
	dict    C.rai_dict_t
	session *Session
}

/* Load the dictionary from the network; dictSubject "" = default; loadWait
 * blocks until done */
func (d *Dict) Load(timeoutSecs int, dictSubject string, loadWait bool) error {
	cs := cstrOpt(dictSubject)
	defer cfree(cs)
	return check(C.rai_dict_load(d.dict, C.uint32_t(timeoutSecs), cs, b2i(loadWait)))
}
func (d *Dict) HaveDict() bool       { return C.rai_dict_have_dict(d.dict) != 0 }
func (d *Dict) InProgress() bool     { return C.rai_dict_in_progress(d.dict) != 0 }
func (d *Dict) GetSession() *Session { return d.session }

/* ---- Entitlement -------------------------------------------------------- */

type Entitlement struct {
	entitle C.rai_entitle_t
}

func (e *Entitlement) Destroy() {
	if e.entitle != nil {
		C.rai_entitle_delete(e.entitle)
		e.entitle = nil
	}
}
