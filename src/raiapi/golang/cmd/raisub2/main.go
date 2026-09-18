/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Go port of src/raiapi/java/raisub2.java (and dotnet/raisub2) */
package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	rai "github.com/injinj/raimdapi/src/raiapi/golang/raiapi2"
)

type raisub2Args struct {
	subject, output, input, save, rotateTime        rai.StringArg
	timeout, rotateIval                             rai.DoubleArg
	snap, listn, retry, noDict, direct, wait, quiet rai.BoolArg
	rate, sched, latency                            rai.BoolArg
	msgCnt                                          rai.IntArg
}

func newRaisub2Args() *raisub2Args {
	return &raisub2Args{
		subject: rai.NewStringArg("subject", "-", "<subject> ...",
			"Subject name(s) to subscribe, use '-' to read subscriptions from stdin"),
		output: rai.NewStringArg("output", "", "<file>", "Output file name, otherwise uses stdout"),
		input:  rai.NewStringArg("input", "", "<file>", "Input file name, otherwise uses stdin"),
		save:   rai.NewStringArg("save", "", "<file>", "Save messages to file in replay format"),
		rotateTime: rai.NewStringArg("rotateTime", "", "<date>",
			"Rotate save messages file at this time"),
		timeout: rai.NewDoubleArg("timeout", 6.0, "<time>",
			"Timeout subscription after this period if no data is received, or "+
				"zero for no timeout"),
		rotateIval: rai.NewDoubleArg("rotateIval", 0.0, "<time>",
			"Rotate save messages file at this interval"),
		snap: rai.NewBoolArg("snap", false, "<bool>", "Get snapshot of subject instead of subscribe"),
		listn: rai.NewBoolArg("listen", false, "<bool>",
			"Listen to subject instead of subscribe, no initial value requested"),
		retry:  rai.NewBoolArg("retry", false, "<bool>", "Retry subscriptions after timeout period"),
		noDict: rai.NewBoolArg("noDict", false, "<bool>", "Don't try to load dictionary"),
		direct: rai.NewBoolArg("direct", false, "<bool>",
			"Whether to dispatch messages directly from the recv threads (true) or "+
				"serialized on the queue thread (false)"),
		wait: rai.NewBoolArg("wait", false, "<bool>",
			"Causes program to keep running after snapshots have completed, use "+
				"this if multiple snapshot replies are expected"),
		quiet: rai.NewBoolArg("quiet", false, "<bool>", "Don't print the messages"),
		rate:  rai.NewBoolArg("rate", false, "<bool>", "Print the rate of messages received"),
		sched: rai.NewBoolArg("sched", false, "<bool>",
			"Read scheduled subscribes and unsubscribes from stdin, useful for "+
				"generating subscription load test"),
		latency: rai.NewBoolArg("latency", false, "<bool>",
			"Track latency of messages and report at program end "),
		msgCnt: rai.NewIntArg("msgCount", 0, "<num>", "Quit after receiving num messages"),
	}
}

func (a *raisub2Args) getArgs(args *rai.Args) {
	args.Add(a.subject, rai.COMMAND_ARG|rai.RESOURCE_ARG|rai.LIST_ARG)
	args.Add(a.snap)
	args.Add(a.listn)
	args.Add(a.timeout, rai.COMMAND_ARG|rai.RESOURCE_ARG|rai.TIME_SEC_ARG)
	args.Add(a.retry)
	args.Add(a.noDict)
	args.Add(a.direct)
	args.Add(a.wait)
	args.Add(a.quiet)
	args.Add(a.rate)
	args.Add(a.sched)
	args.Add(a.latency)
	args.Add(a.msgCnt)
	args.Add(a.output)
	args.Add(a.input)
	args.Add(a.save)
	args.Add(a.rotateTime)
	args.Add(a.rotateIval, rai.COMMAND_ARG|rai.RESOURCE_ARG|rai.TIME_SEC_ARG)
}

const (
	MAX_LAT      = 1000000 /* 100ms */
	MIN_INIT_LAT = 9999999
)

type raisub2 struct {
	api                                                                     *rai.Api       /* the api handle */
	dataDict                                                                *rai.Dict      /* dictionary loader */
	session                                                                 *rai.Session   /* a session */
	queue                                                                   *rai.Queue     /* a queue for message and timer events */
	rateTimer                                                               *rai.Timer     /* a timer used for -rate calculations */
	rotateTimer                                                             *rai.Timer     /* a timer used to rotate -save output */
	inp                                                                     *bufio.Scanner /* subjects read from here */
	inFile                                                                  *os.File
	outp                                                                    *bufio.Writer /* messages written here */
	outFile                                                                 *os.File
	saveOut                                                                 *os.File /* messages written in replay format */
	outName, saveName, inName                                               string
	subjname                                                                string  /* first subject arg */
	inSource                                                                string  /* source name of subjects */
	msgsLat                                                                 []int64 /* map of latency vals < 1 sec */
	latencyOverrun, latencyCnt, cumLatencySum, cumLatencyMin, cumLatencyMax int64
	timeout                                                                 int /* if > 0, then timeout subscription starts */
	msgWaitCount                                                            int /* if > 0, then wait for N messages */
	msgEventCount, msgByteCount, msgTimeoutCount, subCount, unsubCount      int64
	lastTime, baseTime                                                      int64
	lastMsgCount, lastByteCount, lastSubCount, lastUnsubCount               int64
	fileRotate                                                              *rai.TimeRotate
	subHT                                                                   map[string]*rai.Subscribe
	quit                                                                    atomic.Bool
	doRetry, doQuiet, doSnapshot, doListen, doWait, doSched, doLatency      bool
	sigCaught                                                               atomic.Int32
	dispatchDone                                                            chan struct{}
	outLock, saveLock, latLock, subLock, cntLock                            sync.Mutex
}

func newRaisub2() *raisub2 {
	return &raisub2{
		subHT:    map[string]*rai.Subscribe{},
		baseTime: rai.CurrentTimeNanosecs(), /* -save time offset */
	}
}

/* DataLossCallback */
func (t *raisub2) OnConnection(ev *rai.ConnectionEvent, cl any) {
	t.api.PrintLog(rai.LVL_MINOR, nil, "onConnection: "+ev.Description)
}
func (t *raisub2) OnDataLoss(ev *rai.DataLossEvent, cl any) {
	t.api.PrintLog(rai.LVL_ERROR, nil, "onDataLoss: "+ev.Description)
	if ev.ConnectionLoss && ev.ConnectionCount == 0 {
		if err := t.session.NotifyStatus(rai.TRANSIENT, rai.STATUS_TPT_DISCONNECTED); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "NotifyStatus")
		}
	}
}

/* MsgCallback */
func (t *raisub2) OnMsg(ev *rai.MsgEvent, raiMsg *rai.Msg, closure any) {
	var ns int64
	if ev.PubTime != 0 || ev.RouteTime != 0 {
		ns = rai.CurrentTimeNanosecs()
	}
	nBytes := 0
	var err error
	/* track latencies and summarize at program exit */
	if ns != 0 && t.msgsLat != nil && ev.RouteTime != 0 {
		i := (ns - ev.RouteTime) / 100
		nBytes, err = raiMsg.PackSize()
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Pack size")
		}
		/* if -direct = true, then multiple goroutines could callback OnMsg() */
		t.latLock.Lock()
		if i < MAX_LAT {
			t.msgsLat[i]++
			t.cumLatencySum += i
			t.latencyCnt++
			if i < t.cumLatencyMin {
				t.cumLatencyMin = i
			}
			if i > t.cumLatencyMax {
				t.cumLatencyMax = i
			}
		} else {
			t.latencyOverrun++ /* may be cached messages, not deltas */
		}
		t.msgEventCount++
		t.msgByteCount += int64(nBytes)
		t.latLock.Unlock()
	}
	/* print the message */
	if !t.doQuiet {
		s := ev.SubscribedSubject()
		t.outLock.Lock()
		fmt.Fprintf(t.outp, "## Subject %s (old state=%s,new=%s)", s,
			rai.StateToString(ev.OldState), rai.StateToString(ev.State))
		/* if ev.Subject is inbox name, or subscribed subject is wildcard
		 * print the ev.Subject */
		if s != ev.Subject {
			fmt.Fprintf(t.outp, " (%s)", ev.Subject)
		}
		if ev.Counter != 0 {
			fmt.Fprintf(t.outp, " c=%d", ev.Counter) /* msg update count */
		}
		t.outp.WriteString("\n")
		/* if message is timestamped, print times and latencies */
		if ns != 0 {
			fmt.Fprintf(t.outp, "# Receive %s\n", rai.NsTimestamp(ns, 7))
			if ev.PubTime != 0 {
				fmt.Fprintf(t.outp, "# Publish %s (lat=%.5fms)\n", rai.NsTimestamp(ev.PubTime, 7),
					float64(ns-ev.PubTime)/1000000.0)
			}
			if ev.RouteTime != 0 {
				fmt.Fprintf(t.outp, "# Route   %s (lat=%.5fms)\n", rai.NsTimestamp(ev.RouteTime, 7),
					float64(ns-ev.RouteTime)/1000000.0)
			}
		}
		if err = raiMsg.Print(t.outp); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Printing message")
		}
		t.outp.Flush()
		t.outLock.Unlock()
	}
	if t.saveOut != nil {
		/* if message is not internally generated, save it to replay file */
		if ev.RecStatus != rai.STATUS_TIMEOUT && ev.MsgType != rai.SERVICE_STATUS {
			t.saveLock.Lock()
			if err = t.saveMsg(ev, raiMsg, ns); err != nil {
				t.api.PrintLog(rai.LVL_ERROR, err, "Saving message to \""+t.saveName+"\"")
			}
			t.saveLock.Unlock()
		}
	}
	if ev.State == rai.STATE_STALE || ev.RecStatus == rai.STATUS_TIMEOUT {
		why := "timeout"
		if ev.State == rai.STATE_STALE {
			why = "stale"
		}
		if t.doRetry {
			t.api.PrintLog(rai.LVL_MINOR, nil, "Refreshing subject, "+why+": \""+ev.SubscribedSubject()+"\"")
			ev.Subscribe.Refresh(t.timeout)
			return /* don't increment msgEventCount */
		}
		t.cntLock.Lock()
		t.msgTimeoutCount++
		t.cntLock.Unlock()
		t.api.PrintLog(rai.LVL_NORMAL, nil, "Subject "+why+": \""+ev.SubscribedSubject()+"\"")
		return
	}
	/* if tracking latency, nBytes != 0 and these are already computed */
	if nBytes == 0 {
		nBytes, err = raiMsg.PackSize()
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Pack size")
			return
		}
		t.cntLock.Lock()
		t.msgEventCount++
		t.msgByteCount += int64(nBytes)
		t.cntLock.Unlock()
	}
}

func (t *raisub2) saveMsg(ev *rai.MsgEvent, raiMsg *rai.Msg, ns int64) error {
	packed, err := raiMsg.Packed()
	if err != nil {
		return err
	}
	s := ev.Subject /* use ev.Subject unless inbox */
	if strings.HasPrefix(s, "_INBOX") {
		s = ev.SubscribedSubject()
	}
	if ns == 0 {
		ns = rai.CurrentTimeNanosecs()
	}
	if t.baseTime > ns {
		t.baseTime = ns
	}
	if _, err = fmt.Fprintf(t.saveOut, "%s\n%d %.6f\n", s, len(packed),
		float64(ns-t.baseTime)/1000000000.0); err != nil {
		return err
	}
	_, err = t.saveOut.Write(packed)
	return err
}

/* TimerCallback */
func (t *raisub2) OnTimer(timer *rai.Timer, closure any) {
	if timer == t.rateTimer {
		curTime := rai.CurrentTimeMillis()
		interval := float64(curTime-t.lastTime) / 1000.0
		t.lastTime = curTime
		t.cntLock.Lock()
		t.latLock.Lock()
		count := t.msgEventCount
		msgs := count - t.lastMsgCount
		t.lastMsgCount = count
		count = t.msgByteCount
		bytes := count - t.lastByteCount
		t.lastByteCount = count
		t.latLock.Unlock()
		t.cntLock.Unlock()
		cnt := atomic.LoadInt64(&t.subCount)
		subs := cnt - t.lastSubCount
		t.lastSubCount = cnt
		cnt = atomic.LoadInt64(&t.unsubCount)
		unsubs := cnt - t.lastUnsubCount
		t.lastUnsubCount = cnt

		t.outLock.Lock()
		fmt.Fprintf(t.outp, "%.1f sub/s %.1f unsub/s %.1f msg/s %.1f kb/s %.2f mbit/s\n",
			float64(subs)/interval, float64(unsubs)/interval, float64(msgs)/interval,
			float64(bytes)/1024.0/interval, float64(bytes)*8.0/1000.0/1000.0/interval)
		t.outp.Flush()
		t.outLock.Unlock()
	} else if timer == t.rotateTimer && t.fileRotate.CheckRotate() {
		if err := t.rotateSave(); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Rotate file \""+t.saveName+"\"")
		}
	}
}

func (t *raisub2) rotateSave() error {
	if err := t.renameSaveFile(); err != nil {
		return err
	}
	tmpOut := t.saveOut
	newOut, err := os.Create(t.saveName)
	if err != nil {
		return err
	}
	nextRotate, _ := t.fileRotate.NextRotate()
	t.api.PrintLog(rai.LVL_MINOR, nil, "File \""+t.saveName+"\" rotate: interval: "+
		rai.MsIntervalTime(t.fileRotate.GetInterval())+"; next: "+rai.MsTimestamp(nextRotate, 0))
	t.saveLock.Lock()
	t.baseTime = rai.CurrentTimeNanosecs()
	t.saveOut = newOut
	t.saveLock.Unlock()
	return tmpOut.Close()
}

func (t *raisub2) renameSaveFile() error {
	curTime := rai.CurrentTimeMillis()
	saveName2 := t.saveName + rai.Strftime(rai.TZ_LOCAL_TIME, curTime, ".%Y-%m-%d_%H-%M-%S")
	return os.Rename(t.saveName, saveName2)
}

func (t *raisub2) init(api *rai.Api, args *rai.Args) bool {
	t.api = api
	if err := t.initErr(args); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Not initialized, stopped")
		return false
	}
	return true
}

func (t *raisub2) initErr(args *rai.Args) error {
	subargs := newRaisub2Args()

	noDictionary := args.Bool(subargs.noDict.Name)

	t.subjname = args.String(subargs.subject.Name)
	t.outName = args.String(subargs.output.Name)
	t.saveName = args.String(subargs.save.Name)
	t.inName = args.String(subargs.input.Name)
	t.doSnapshot = args.Bool(subargs.snap.Name)
	t.doListen = args.Bool(subargs.listn.Name)
	t.doWait = args.Bool(subargs.wait.Name)
	t.doSched = args.Bool(subargs.sched.Name)
	t.msgWaitCount = args.Int(subargs.msgCnt.Name)
	t.doLatency = args.Bool(subargs.latency.Name)

	if t.doLatency {
		t.msgsLat = make([]int64, MAX_LAT)
		t.cumLatencyMin = MIN_INIT_LAT
	}

	rotateIval := int64(args.Double(subargs.rotateIval.Name)*1000.0 + 0.5)

	t.fileRotate = rai.NewTimeRotate()
	t.fileRotate.SetRotateTime(args.String(subargs.rotateTime.Name), rai.ROTATE_UNSPECIFIED, 0)
	t.fileRotate.SetRotatePeriod("", rotateIval)
	t.fileRotate.SetLastTime(rai.CurrentTimeMillis())
	nextRotate, _ := t.fileRotate.NextRotate()

	if t.saveName != "" {
		if nextRotate != 0 {
			if _, err := os.Stat(t.saveName); err == nil {
				t.renameSaveFile()
			}
		}
		f, err := os.Create(t.saveName)
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Opening \""+t.saveName+"\"")
			return rai.NewError("Open " + t.saveName)
		}
		t.saveOut = f
	}
	if t.outName != "" {
		f, err := os.Create(t.outName)
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Opening \""+t.outName+"\"")
			return rai.NewError("Open " + t.outName)
		}
		t.outFile = f
		t.outp = bufio.NewWriter(f)
	}
	if t.outp == nil {
		t.outp = bufio.NewWriter(os.Stdout)
	}

	rai.OpenLogArgs(args)
	/* if cfilePath specified on the command line */
	if !noDictionary {
		loaded, err := rai.OpenDict(args)
		if err != nil {
			return err
		}
		noDictionary = loaded
	}
	if err := t.api.ParseArgs(args); err != nil {
		return err
	}

	t.timeout = int(args.Double(subargs.timeout.Name) * 1000.0)
	t.doRetry = args.Bool(subargs.retry.Name)
	t.doQuiet = args.Bool(subargs.quiet.Name)
	var err error
	if t.session, err = t.api.CreateSession(); err != nil {
		return err
	}
	if err = t.session.SetDataLossCB(t, nil); err != nil {
		return err
	}
	if err = t.session.Start(); err != nil {
		return err
	}

	/* resolve dictionary */
	if !noDictionary {
		if t.dataDict, err = t.session.CreateDict(); err != nil {
			return err
		}
		if err = t.dataDict.Load(3, "", false); err != nil {
			return err
		}
		for t.dataDict.InProgress() && !t.quit.Load() {
			time.Sleep(time.Millisecond)
		}
		if t.quit.Load() {
			return rai.NewError("quit")
		}
		if !t.dataDict.HaveDict() {
			if err = t.dataDict.Load(10, "", false); err != nil {
				return err
			}
			for t.dataDict.InProgress() && !t.quit.Load() {
				time.Sleep(time.Millisecond)
			}
			if t.quit.Load() {
				return rai.NewError("quit")
			}
			if !t.dataDict.HaveDict() {
				t.api.PrintLog(rai.LVL_MINOR, nil, "Dictionary load timed out")
				t.outp.WriteString("Dictionary load timed out\n")
				t.outp.Flush()
				return rai.NewError("Dictionary load timed out")
			}
		}
		if !t.doQuiet {
			t.outp.WriteString("Dictionary received\n")
			t.outp.Flush()
		}
	}

	if t.subjname != "" && t.subjname[0] != '-' {
		var b strings.Builder
		b.WriteString(t.subjname)
		b.WriteByte('\n')
		n := args.GetNumValues(subargs.subject.Name)
		for i := 1; i < n; i++ {
			b.WriteString(args.String(subargs.subject.Name, i))
			b.WriteByte('\n')
		}
		t.inp = bufio.NewScanner(strings.NewReader(b.String()))
		t.inSource = "cmdline"
	} else if t.inName != "" {
		f, err := os.Open(t.inName)
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Opening \""+t.inName+"\"")
			return rai.NewError("Open " + t.inName)
		}
		t.inFile = f
		t.inp = bufio.NewScanner(f)
		t.inSource = t.inName
	} else {
		t.inp = bufio.NewScanner(os.Stdin)
		t.inSource = "stdin"
	}
	/* create queue, direct = true will cause messages to be dispatched
	 * directly from the network instead of the queue */
	if t.queue, err = t.session.CreateQueue(args.Bool(subargs.direct.Name)); err != nil {
		return err
	}
	if args.Bool(subargs.rate.Name) {
		/* print message rate every .5 secs in a timer */
		if t.rateTimer, err = t.queue.CreateTimer(t, nil); err != nil {
			return err
		}
		t.rateTimer.SetInterval(500)
		t.lastTime = rai.CurrentTimeMillis()
		if err = t.rateTimer.Start(); err != nil {
			return err
		}
	}
	if t.saveOut != nil && nextRotate != 0 {
		t.api.PrintLog(rai.LVL_MINOR, nil, "File \""+t.saveName+"\" rotate: interval: "+
			rai.MsIntervalTime(t.fileRotate.GetInterval())+"; next: "+rai.MsTimestamp(nextRotate, 0))
		if t.rotateTimer, err = t.queue.CreateTimer(t, nil); err != nil {
			return err
		}
		t.rotateTimer.SetInterval(10000)
		if err = t.rotateTimer.Start(); err != nil {
			return err
		}
	}
	return nil
}

func (t *raisub2) subscribe(subject string, parm int) bool {
	newSub, err := t.queue.CreateSubscribe(t, nil)
	if err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Subscribe "+subject)
		return false
	}
	if !t.doQuiet {
		what := "Starting"
		if parm == rai.SUB_SNAP {
			what = "Snapshot"
		} else if parm == rai.SUB_UPDATE {
			what = "Listening"
		}
		t.outLock.Lock()
		fmt.Fprintf(t.outp, "%s subject %s\n", what, subject)
		t.outp.Flush()
		t.outLock.Unlock()
	}
	t.subLock.Lock()
	t.subHT[subject] = newSub
	t.subLock.Unlock()
	if err = newSub.Start(subject, parm|rai.SUB_NO_COPY, t.timeout); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Subscribe "+subject)
		return false
	}
	atomic.AddInt64(&t.subCount, 1)
	return true
}

func (t *raisub2) unsubscribe(subject string) bool {
	t.subLock.Lock()
	sub, ok := t.subHT[subject]
	if ok {
		delete(t.subHT, subject)
	}
	t.subLock.Unlock()
	if !ok {
		return false
	}
	if !t.doQuiet {
		t.outLock.Lock()
		fmt.Fprintf(t.outp, "Unsubscribe subject %s\n", subject)
		t.outp.Flush()
		t.outLock.Unlock()
	}
	if err := sub.Cancel(); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Unsubscribe "+subject)
		return false
	}
	atomic.AddInt64(&t.unsubCount, 1)
	return true
}

func (t *raisub2) dispatchLoop() {
	for !t.quit.Load() {
		if err := t.queue.TimedDispatch(100); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "In dispatchLoop")
		}
	}
	t.api.PrintLog(rai.LVL_MINOR, nil, "Done dispatchLoop")
	close(t.dispatchDone)
}

/* goroutine from main() for dispatch loop */
func (t *raisub2) start() {
	t.dispatchDone = make(chan struct{})
	go t.dispatchLoop()
}
func (t *raisub2) join() {
	if t.dispatchDone != nil {
		<-t.dispatchDone
	}
}

/* Subscribe subjects, then wait for quit (ctrl-c) or until message count
 * received */
func (t *raisub2) subLoop() {
	parm := rai.SUB_BOTH
	if t.doSnapshot {
		parm = rai.SUB_SNAP
	} else if t.doListen {
		parm = rai.SUB_UPDATE
	}
	count, missSub, missUnsub := 0, 0, 0

	if !t.doSched {
		/* subscribe or snap subjects are read from stdin */
		for !t.quit.Load() && t.inp.Scan() {
			s := t.inp.Text()
			if len(s) > 0 {
				if t.subscribe(s, parm) {
					count++
				} else {
					missSub++
				}
			}
		}
		if !t.doQuiet {
			t.outLock.Lock()
			fmt.Fprintf(t.outp, "%d subjects read on %s\n", count, t.inSource)
			t.outp.Flush()
			t.outLock.Unlock()
		}
	} else {
		/* read "SUB subject" and "UNSUB subject" commands from input,
		 * if not a command and line is an integer, sleep */
		for !t.quit.Load() && t.inp.Scan() {
			s := t.inp.Text()
			if len(s) > 0 {
				if strings.HasPrefix(s, "SUB ") {
					if t.subscribe(s[4:], parm) {
						count++
					} else {
						missSub++
					}
				} else if strings.HasPrefix(s, "UNSUB ") {
					if t.unsubscribe(s[6:]) {
						count--
					} else {
						missUnsub++
					}
				} else if s[0] >= '0' && s[0] <= '9' {
					if i, err := strconv.Atoi(s); err == nil && i > 0 {
						time.Sleep(time.Duration(i) * time.Second)
					}
				}
			}
		}
		t.outLock.Lock()
		fmt.Fprintf(t.outp, "Done reading SUB/UNSUB commands on stdin, waiting for %d subs\n", count)
		t.outp.Flush()
		t.outLock.Unlock()
	}
	if t.msgWaitCount == 0 {
		if t.doSnapshot && !t.doWait {
			t.subLock.Lock()
			t.msgWaitCount = len(t.subHT)
			t.subLock.Unlock()
		}
	}
	if t.msgWaitCount > 0 {
		for !t.quit.Load() {
			/* if all messages rcvd and/or timed out */
			t.cntLock.Lock()
			t.latLock.Lock()
			done := t.msgWaitCount > 0 &&
				int64(t.msgWaitCount) <= t.msgEventCount+t.msgTimeoutCount
			t.latLock.Unlock()
			t.cntLock.Unlock()
			if done {
				t.quit.Store(true)
			} else {
				time.Sleep(100 * time.Millisecond)
			}
		}
	}
	if t.doQuiet && t.msgTimeoutCount > 0 {
		t.api.PrintLog(rai.LVL_ERROR, nil, fmt.Sprintf("%d subjects timeout (%d recv)",
			t.msgTimeoutCount, t.msgEventCount))
	}
	if missSub > 0 {
		t.api.PrintLog(rai.LVL_ERROR, nil, fmt.Sprintf("%d subjects did not subscribe", missSub))
	}
	if missUnsub > 0 {
		t.api.PrintLog(rai.LVL_ERROR, nil, fmt.Sprintf("%d subjects did not unsubscribe", missUnsub))
	}
}

func (t *raisub2) finalLat() {
	var av float64
	if t.latencyCnt != 0 {
		av = float64(t.cumLatencySum) / float64(t.latencyCnt)
	}
	avg := int(av)
	if avg >= MAX_LAT {
		avg = MAX_LAT - 1
	}
	cnt := t.msgsLat[avg]
	stdDev := [4]int{1, 0, 0, 0}
	stdDevMsgs := [4]int64{0, int64(float64(t.latencyCnt) * 0.682),
		int64(float64(t.latencyCnt) * 0.955), int64(float64(t.latencyCnt) * 0.997)}

	for j := 1; j < 4; j++ {
		for stdDev[j] = stdDev[j-1]; stdDev[j] < MAX_LAT && cnt < stdDevMsgs[j]; stdDev[j]++ {
			if stdDev[j] <= avg {
				cnt += t.msgsLat[avg-stdDev[j]]
			}
			if avg+stdDev[j] < MAX_LAT {
				cnt += t.msgsLat[avg+stdDev[j]]
			}
		}
	}
	t.api.PrintLog(rai.LVL_NORMAL, nil, fmt.Sprintf(
		"av=%.6f min=%.6f max=%.6f stddev=(%.6f,%.6f,%.6f) (68.2%%,95.5%%,99.7%%)",
		av/10000.0, float64(t.cumLatencyMin)/10000.0, float64(t.cumLatencyMax)/10000.0,
		float64(stdDev[1])/10000.0, float64(stdDev[2])/10000.0, float64(stdDev[3])/10000.0))
	t.api.PrintLog(rai.LVL_NORMAL, nil, fmt.Sprintf("latency overruns: %d > %dms",
		t.latencyOverrun, MAX_LAT/1000/10))
}

/* close everything */
func (t *raisub2) close() {
	if t.doLatency {
		t.finalLat()
	}
	t.quit.Store(true)
	if t.rateTimer != nil {
		t.rateTimer.Stop()
	}
	if t.rotateTimer != nil {
		t.rotateTimer.Stop()
	}
	t.subLock.Lock()
	subs := make([]*rai.Subscribe, 0, len(t.subHT))
	for _, s := range t.subHT {
		subs = append(subs, s)
	}
	t.subHT = map[string]*rai.Subscribe{}
	t.subLock.Unlock()
	for _, s := range subs {
		s.Cancel()
	}
	if t.queue != nil {
		t.queue.Destroy()
	}
	if t.session != nil {
		t.session.Destroy()
	}
	if t.api != nil {
		t.api.Close()
	}
	if t.outp != nil {
		t.outp.Flush()
	}
	if t.outFile != nil {
		t.outFile.Close()
	}
	if t.saveOut != nil {
		t.saveOut.Close()
	}
	if t.inFile != nil {
		t.inFile.Close()
	}
}

var subTest *raisub2

func sigHandler(sig int) {
	if subTest != nil {
		subTest.sigCaught.Store(int32(sig))
		subTest.quit.Store(true)
	} else {
		os.Exit(1)
	}
}

func main() {
	argv := os.Args[1:]
	/* traps SIGINT, SIGHUP, SIGTERM and calls sigHandler() */
	rai.RegisterSigHandler(sigHandler)
	/* open log to stderr in case command line fails to parse, it may open
	 * again if -log is specified on command line */
	rai.OpenLog("-", rai.LVL_MINOR, 4)
	/* Open the api type from the command line, looks for -api <name> in
	 * argv[] and loads that middleware.  Program could also pass "tibrv" or
	 * some other api name in the first argument.  If neither are specfied
	 * then the default api is loaded */
	api, err := rai.RaiOpen("", argv)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Unable to load Rai API: "+err.Error())
		os.Exit(1)
	}

	if err = run(api, argv); err != nil {
		rai.Log(rai.LVL_ERROR, err, "Main")
	}
	rai.Log(rai.LVL_MINOR, nil, "Finished")
}

func run(api *rai.Api, argv []string) error {
	args, err := rai.NewArgs()
	if err != nil {
		return err
	}
	subargs := newRaisub2Args()

	/* get the api's configuration arguments */
	if err = api.GetArgs(args); err != nil {
		return err
	}
	/* get the subject and settings for the program */
	subargs.getArgs(args)
	/* get the arguments for the dictionary, useful for parsing dict files
	 * locally in the filesystem instead of receiving it on the network */
	if err = rai.GetDictArgs(args); err != nil {
		return err
	}
	/* get the logging, version, help, rc arguments and sets error output */
	if err = args.AddDefaults(rai.RaiVersion(), "rai_", os.Stderr, "raisub2"); err != nil {
		return err
	}
	/* If -help or -version specified in argv[], then ProcessArgs() returns
	 * false and program exits without executing. */
	ok, err := args.ProcessArgs(argv)
	if err != nil {
		return err
	}
	if ok {
		subTest = newRaisub2()
		/* create api elements and start the dispatch goroutine */
		if subTest.init(api, args) {
			subTest.start()
			/* subscribes */
			subTest.subLoop()
			subTest.join() /* join mainloop dispatch goroutine */
		}
		if sig := subTest.sigCaught.Load(); sig != 0 {
			rai.Log(rai.LVL_MINOR, nil, fmt.Sprintf("Caught signal %d, quitting", sig))
		}
		/* stop all the subscribes, if any, close the api */
		subTest.close()
	}
	return nil
}

var _ io.Writer = (*bufio.Writer)(nil)
