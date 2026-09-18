/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Go port of src/raiapi/java/raiping2.java (and dotnet/raiping2) */
package main

import (
	"fmt"
	"os"
	"sync/atomic"

	rai "github.com/injinj/raimdapi/src/raiapi/golang/raiapi2"
)

type raiping2Args struct {
	perSec, msgCount     rai.IntArg
	prefix, subject      rai.StringArg
	direct, noSub, noPub rai.BoolArg
}

func newRaiping2Args() *raiping2Args {
	return &raiping2Args{
		perSec:   rai.NewIntArg("perSec", 10, "<num>", "Number of msgs per sec"),
		msgCount: rai.NewIntArg("msgCount", 0, "<num>", "Number of msgs to publish, 0 for infinite"),
		prefix: rai.NewStringArg("prefix", "", "<subject>",
			"Subject to prefix publish subject with, usually set to _TIC. if using SASS/RV"),
		subject: rai.NewStringArg("subject", "PING.TEST.REC.XXX", "<subject>", "Subject to ping"),
		direct: rai.NewBoolArg("direct", false, "<bool>",
			"Whether to dispatch messages directly from the recv threads (true) or "+
				"serialized on the queue thread (false)"),
		noSub: rai.NewBoolArg("noSub", false, "<bool>", "Don't subscribe, only publish pings"),
		noPub: rai.NewBoolArg("noPub", false, "<bool>", "Don't publish, only subscribe pings"),
	}
}

func (a *raiping2Args) getArgs(args *rai.Args) {
	args.Add(a.perSec)
	args.Add(a.msgCount)
	args.Add(a.prefix)
	args.Add(a.subject)
	args.Add(a.direct)
	args.Add(a.noSub)
	args.Add(a.noPub)
}

const (
	MAX_LAT      = 1000000 /* 100ms */
	MIN_INIT_LAT = 9999999
)

type raiping2 struct {
	api                      *rai.Api
	session                  *rai.Session
	subQueue, pubQueue       *rai.Queue
	subscriber               *rai.Subscribe
	publisher                *rai.Publish
	publishTimer, printTimer *rai.Timer
	/* set when a RAIPING form is available in the dictionary (see the java
	 * version); "" means the plain MSG_TYPE/SEQ_NO/REC_STATUS/time header */
	msgTypeField, recTypeField, seqNoField, recStatusField, timeField               string
	subject, prefix, publishSubject                                                 string
	msgsPerSec                                                                      int
	msgsPublished, msgsClocked, msgCount, msgSent, msgRecvd, lastMsgRecvd           int64
	intervalStart, startTime                                                        int64
	latencySum, latencyMin, latencyMax, cumLatencySum, cumLatencyMin, cumLatencyMax float64
	recType                                                                         uint16
	direct, noSub, noPub                                                            bool
	quit                                                                            atomic.Bool
	sigCaught                                                                       atomic.Int32
	msgsLat                                                                         []int64 /* 1us .. 100ms */
	pubDone                                                                         chan struct{}
	pingMsg                                                                         *rai.Msg
}

func newRaiping2() *raiping2 {
	return &raiping2{
		msgsLat:       make([]int64, MAX_LAT),
		latencyMin:    MIN_INIT_LAT,
		cumLatencyMin: MIN_INIT_LAT,
		msgsPerSec:    10,
	}
}

func (t *raiping2) close() {
	t.finalLat()
	t.quit.Store(true)
	if t.pubDone != nil {
		<-t.pubDone
	}
	if t.publishTimer != nil {
		t.publishTimer.Stop()
	}
	if t.printTimer != nil {
		t.printTimer.Stop()
	}
	if t.publisher != nil {
		t.publisher.Destroy()
	}
	if t.subscriber != nil {
		t.subscriber.Cancel()
	}
	if t.subQueue != nil {
		t.subQueue.Destroy()
	}
	if t.pubQueue != nil {
		t.pubQueue.Destroy()
	}
	if t.session != nil {
		t.session.Destroy()
	}
	if t.api != nil {
		t.api.Close()
	}
	if t.pingMsg != nil {
		t.pingMsg.Delete()
	}
}

func (t *raiping2) init(api *rai.Api, args *rai.Args) bool {
	t.api = api
	if err := t.initErr(args); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Not initialized, stopped")
		return false
	}
	return true
}

func (t *raiping2) initErr(args *rai.Args) error {
	pargs := newRaiping2Args()

	rai.OpenLogArgs(args)
	if err := t.api.ParseArgs(args); err != nil {
		return err
	}

	t.subject = args.String(pargs.subject.Name)
	t.prefix = args.String(pargs.prefix.Name)
	t.msgCount = int64(args.Int(pargs.msgCount.Name))
	t.msgsPerSec = args.Int(pargs.perSec.Name)
	t.direct = args.Bool(pargs.direct.Name)
	t.noSub = args.Bool(pargs.noSub.Name)
	t.noPub = args.Bool(pargs.noPub.Name)

	t.publishSubject = t.prefix + t.subject
	if !t.noPub && !t.noSub {
		t.api.PrintLog(rai.LVL_MINOR, nil, "Publishing "+t.publishSubject+" subscribe "+t.subject)
	} else if t.noPub {
		t.api.PrintLog(rai.LVL_MINOR, nil, "Subscribe "+t.subject)
	} else if t.noSub {
		t.api.PrintLog(rai.LVL_MINOR, nil, "Publishing "+t.publishSubject)
	}
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
	if t.subQueue, err = t.session.CreateQueue(t.direct); err != nil {
		return err
	}
	if t.pubQueue, err = t.session.CreateQueue(t.direct); err != nil {
		return err
	}

	if !t.noSub {
		if t.subscriber, err = t.subQueue.CreateSubscribe(t, nil); err != nil {
			return err
		}
		if err = t.subscriber.Start(t.subject, rai.SUB_UPDATE|rai.SUB_NO_COPY, 0); err != nil {
			return err
		}
	}
	if !t.noPub {
		if t.publisher, err = t.session.CreatePublish(false); err != nil {
			return err
		}
		/* timer is 10 times faster than rate since pub rate is controlled by
		 * updateClock(), not by this timer */
		timeout := 1
		if t.msgsPerSec < 100 {
			timeout = 100 / t.msgsPerSec
		}
		if t.publishTimer, err = t.pubQueue.CreateTimer(t, nil); err != nil {
			return err
		}
		t.publishTimer.SetInterval(int64(timeout))
		if err = t.publishTimer.Start(); err != nil {
			return err
		}
	} else {
		/* since updateClock() is never called, and OnMsg() tests if messages
		 * are published before calculating latency */
		t.startTime = 1
	}
	if t.msgsPerSec > 15 || t.noPub {
		if t.printTimer, err = t.pubQueue.CreateTimer(t, nil); err != nil {
			return err
		}
		t.printTimer.SetInterval(500) /* ms */
		t.intervalStart = rai.HiresTimeNanosecs()
		if err = t.printTimer.Start(); err != nil {
			return err
		}
	}
	return nil
}

/* DataLossCallback */
func (t *raiping2) OnConnection(ev *rai.ConnectionEvent, cl any) {
	t.api.PrintLog(rai.LVL_MINOR, nil, "onConnection: "+ev.Description)
}
func (t *raiping2) OnDataLoss(ev *rai.DataLossEvent, cl any) {
	t.api.PrintLog(rai.LVL_ERROR, nil, "onDataLoss: "+ev.Description)
}

func (t *raiping2) updateClock() int64 {
	currentTime := rai.HiresTimeNanosecs()
	if t.startTime == 0 {
		t.startTime = currentTime
		t.msgsClocked = 0
	} else if currentTime > t.startTime {
		t.msgsClocked = int64(float64(currentTime-t.startTime) / 1000000000.0 * float64(t.msgsPerSec))
	}
	return currentTime
}

/* MsgCallback */
func (t *raiping2) OnMsg(ev *rai.MsgEvent, raiMsg *rai.Msg, closure any) {
	sendTime, err := raiMsg.GetLong("time")
	if err != nil {
		if !rai.IsNotFound(err) {
			t.api.PrintLog(rai.LVL_ERROR, err, "Getting time field")
		}
		return
	}
	if sendTime < t.startTime || t.startTime == 0 { /* old ping value */
		return
	}
	curTime := rai.HiresTimeNanosecs()
	latencyMS := float64(curTime-sendTime) / 1000000.0
	t.latencySum += latencyMS
	t.cumLatencySum += latencyMS
	usecsIndex := int(latencyMS * 1000.0)
	if usecsIndex < 0 {
		usecsIndex = 0
	} else if usecsIndex >= MAX_LAT {
		usecsIndex = MAX_LAT - 1
	}
	t.msgsLat[usecsIndex]++

	if latencyMS > t.latencyMax {
		t.latencyMax = latencyMS
		if latencyMS > t.cumLatencyMax {
			t.cumLatencyMax = latencyMS
		}
	}
	if latencyMS < t.latencyMin {
		t.latencyMin = latencyMS
		if latencyMS < t.cumLatencyMin {
			t.cumLatencyMin = latencyMS
		}
	}
	t.msgRecvd++

	if t.printTimer == nil {
		fmt.Printf("%s cnt=%d latency=%.3f\n", ev.Subject, t.msgRecvd, latencyMS)
	}
	if t.msgRecvd == t.msgCount {
		t.quit.Store(true)
	}
}

/* TimerCallback */
func (t *raiping2) OnTimer(timer *rai.Timer, closure any) {
	if timer == t.publishTimer {
		t.publish()
	} else if timer == t.printTimer {
		t.print()
	}
}

func (t *raiping2) publish() {
	if err := t.publishErr(); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "publish")
	}
}

func (t *raiping2) publishErr() error {
	curTime := t.updateClock()
	for ; t.msgsPublished < t.msgsClocked; t.msgsPublished++ {
		var err error
		if t.pingMsg == nil {
			if t.pingMsg, err = rai.NewMsg(rai.RAIMSG_PROTO); err != nil {
				return err
			}
			if t.timeField != "" {
				t.pingMsg.AppendUShort(t.msgTypeField, uint16(rai.UPDATE))
				t.pingMsg.AppendUShort(t.recTypeField, t.recType)
				t.pingMsg.AppendUShort(t.seqNoField, uint16(t.msgSent))
				t.pingMsg.AppendUShort(t.recStatusField, 0)
				err = t.pingMsg.AppendULong(t.timeField, uint64(curTime))
			} else {
				t.pingMsg.AppendUShort("MSG_TYPE", uint16(rai.UPDATE))
				t.pingMsg.AppendUShort("SEQ_NO", uint16(t.msgSent))
				t.pingMsg.AppendUShort("REC_STATUS", 0)
				err = t.pingMsg.AppendULong("time", uint64(curTime))
			}
		} else {
			if t.timeField != "" {
				t.pingMsg.UpdateUShort(t.seqNoField, uint16(t.msgSent))
				err = t.pingMsg.UpdateULong(t.timeField, uint64(curTime))
			} else {
				t.pingMsg.UpdateUShort("SEQ_NO", uint16(t.msgSent))
				err = t.pingMsg.UpdateULong("time", uint64(curTime))
			}
		}
		if err != nil {
			return err
		}
		if err = t.publisher.PublishMsg(t.publishSubject, t.pingMsg, 0); err != nil {
			return err
		}
		t.msgSent++
		if t.msgSent == t.msgCount {
			t.publishTimer.Stop()
			return nil
		}
		curTime = rai.HiresTimeNanosecs()
	}
	return nil
}

func rateFmt(rate float64) string {
	suffix := ""
	if rate >= 950.0 {
		rate /= 1000.0
		suffix = "k"
	}
	switch {
	case rate >= 10000.0:
		return fmt.Sprintf("%.0f%s", rate, suffix)
	case rate >= 1000.0:
		return fmt.Sprintf("%.1f%s", rate, suffix)
	default:
		return fmt.Sprintf("%.2f%s", rate, suffix)
	}
}

func (t *raiping2) print() {
	curTime := rai.HiresTimeNanosecs()
	interval := float64(curTime-t.intervalStart) / 1000000000.0
	if interval >= 0.100 {
		if t.msgRecvd > t.lastMsgRecvd {
			t.intervalStart = curTime
			rate := float64(t.msgRecvd - t.lastMsgRecvd)
			t.lastMsgRecvd = t.msgRecvd
			ms := t.latencySum / rate
			rate /= interval
			min, max := t.latencyMin, t.latencyMax
			t.latencySum = 0
			t.latencyMin = MIN_INIT_LAT
			t.latencyMax = 0

			if rate < 30.0 {
				if rate <= 1.1 {
					fmt.Printf("%s: %.3f ms\n", t.subject, ms)
				} else {
					fmt.Printf("%s: rate=%.1f/s av=%.3f min=%.3f max=%.3fms\n", t.subject, rate, ms, min, max)
				}
			} else {
				fmt.Printf("%s: r=%s/s av=%.3f min=%.3f max=%.3fms\n", t.subject, rateFmt(rate), ms, min, max)
			}
		}
	}
}

func (t *raiping2) finalLat() {
	if t.noSub {
		return
	}
	var av float64
	if t.msgRecvd != 0 {
		av = t.cumLatencySum / float64(t.msgRecvd)
	}
	avg := int(av * 1000.0)
	if avg >= MAX_LAT {
		avg = MAX_LAT - 1
	}
	stdDev := [4]int{1, 0, 0, 0}
	stdDevMsgs := [4]int64{0, int64(float64(t.msgRecvd) * 0.682),
		int64(float64(t.msgRecvd) * 0.955), int64(float64(t.msgRecvd) * 0.997)}
	cnt := t.msgsLat[avg]
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
	fmt.Printf("av=%.6f min=%.6f max=%.6f stddev=(%.6f,%.6f,%.6f) (68.2%%,95.5%%,99.7%%)\n",
		av, t.cumLatencyMin, t.cumLatencyMax,
		float64(stdDev[1])/1000.0, float64(stdDev[2])/1000.0, float64(stdDev[3])/1000.0)
	if av > float64(MAX_LAT)/1000.0 {
		fmt.Println("stddev not accurate")
	}
}

/* use two goroutines, one for publishing / printing and one for subscribe */
func (t *raiping2) publoop() {
	for !t.quit.Load() {
		if err := t.pubQueue.TimedDispatch(100); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "pubQueue dispatch")
		}
	}
	close(t.pubDone)
}
func (t *raiping2) dispatchLoop() {
	t.pubDone = make(chan struct{})
	go t.publoop()
	for !t.quit.Load() {
		if err := t.subQueue.TimedDispatch(100); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "subQueue dispatch")
		}
	}
}

var ping *raiping2

func sigHandler(sig int) {
	if ping != nil {
		ping.sigCaught.Store(int32(sig))
		ping.quit.Store(true)
	} else {
		os.Exit(1)
	}
}

func main() {
	argv := os.Args[1:]
	rai.RegisterSigHandler(sigHandler)
	rai.OpenLog("-", rai.LVL_MINOR, 4)
	api, err := rai.RaiOpen("", argv)
	if err != nil {
		fmt.Fprintln(os.Stderr, "Unable to load Rai API: "+err.Error())
		os.Exit(1)
	}
	if err = run(api, argv); err != nil {
		rai.Log(rai.LVL_ERROR, err, "Main")
	}
}

func run(api *rai.Api, argv []string) error {
	args, err := rai.NewArgs()
	if err != nil {
		return err
	}
	pargs := newRaiping2Args()
	/* get the api's configuration arguments */
	if err = api.GetArgs(args); err != nil {
		return err
	}
	/* get the ping subject and rate */
	pargs.getArgs(args)
	/* get the logging, version, help, rc arguments and sets error output */
	if err = args.AddDefaults(rai.RaiVersion(), "rai_", os.Stderr, "raiping2"); err != nil {
		return err
	}
	/* match command line args, if -help or -version, returns false */
	ok, err := args.ProcessArgs(argv)
	if err != nil {
		return err
	}
	if ok {
		ping = newRaiping2()
		/* initialize ping subscriptions, publisher, queue */
		if ping.init(api, args) {
			ping.dispatchLoop() /* run ping queue dispatch */
		}
		/* print ping latency summary and close api handles */
		ping.close()
	}
	return nil
}
