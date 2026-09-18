/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Go port of src/raiapi/java/raireplay2.java (and dotnet/raireplay2) */
package main

import (
	"fmt"
	"os"
	"sync/atomic"

	rai "github.com/injinj/raimdapi/src/raiapi/golang/raiapi2"
)

type raireplay2Args struct {
	fileName, prefix  rai.StringArg
	perSec, msgCount  rai.IntArg
	publishOnce, rate rai.BoolArg
	realtime          rai.DoubleArg
}

func newRaireplay2Args() *raireplay2Args {
	return &raireplay2Args{
		fileName:    rai.NewStringArg("fileName", "", "<file> [<file> ...]", "Replay file name(s)"),
		perSec:      rai.NewIntArg("perSec", 1, "<num>", "Number of msgs to replay per second"),
		msgCount:    rai.NewIntArg("msgCount", 0, "<num>", "Number of msgs to publish, 0 for infinite"),
		publishOnce: rai.NewBoolArg("once", false, "", "Don't rewind files, publish records only one time"),
		prefix: rai.NewStringArg("prefix", "", "<subject>",
			"Publish subject prefix, usually set to _TIC. if using SASS/RV"),
		rate: rai.NewBoolArg("rate", false, "", "Display publish rate info"),
		realtime: rai.NewDoubleArg("realtime", 0.0, "<speed>",
			"Replay messages at the speed that they were recorded (0 = use -perSec, "+
				"1 = 1x record speed, 2.5 = 2.5x record speed)"),
	}
}

func (a *raireplay2Args) getArgs(args *rai.Args) {
	args.Add(a.fileName, rai.COMMAND_ARG|rai.RESOURCE_ARG|rai.LIST_ARG)
	args.Add(a.perSec)
	args.Add(a.msgCount)
	args.Add(a.publishOnce)
	args.Add(a.prefix)
	args.Add(a.rate)
	args.Add(a.realtime)
}

type raireplay2 struct {
	api                                                        *rai.Api
	session                                                    *rai.Session
	pubQueue                                                   *rai.Queue
	pub                                                        *rai.Publish
	pubTimer, printTimer, startTimer                           *rai.Timer
	msgCount                                                   int
	msgsClocked, msgsSent, bytesSent, msgsPrint, ivalBytesSent int64
	msgsPerSec, fileNum, fileCount, errCount                   int
	inp                                                        *os.File
	startTime, currentTime                                     int64
	files                                                      []string
	msgBuf                                                     []byte
	bufOff, bufLen, msgOff, msgSize                            int
	subject                                                    string
	intervalStart, baseTime                                    int64
	realtimeSpeed, msgDelta                                    float64
	sigCaught                                                  atomic.Int32
	publishOnce, printRate                                     bool
	quit                                                       atomic.Bool
}

func newRaireplay2() *raireplay2 { return &raireplay2{msgBuf: make([]byte, 8*1024)} }

func (t *raireplay2) close() {
	t.quit.Store(true)
	if t.pubTimer != nil {
		t.pubTimer.Stop()
	}
	if t.printTimer != nil {
		t.printTimer.Stop()
	}
	if t.startTimer != nil {
		t.startTimer.Stop()
	}
	if t.pub != nil {
		t.pub.Destroy()
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
	t.closeInput()
}

func (t *raireplay2) init(api *rai.Api, args *rai.Args) bool {
	t.api = api
	if err := t.initErr(args); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Not initialized, stopped")
		return false
	}
	return true
}

func (t *raireplay2) initErr(args *rai.Args) error {
	repargs := newRaireplay2Args()

	t.fileCount = args.GetNumValues(repargs.fileName.Name)
	if t.fileCount == 0 {
		return rai.NewError("No file, -fileName required")
	}

	rai.OpenLogArgs(args)
	if err := t.api.ParseArgs(args); err != nil {
		return err
	}

	t.files = make([]string, t.fileCount)
	for i := 0; i < t.fileCount; i++ {
		t.files[i] = args.String(repargs.fileName.Name, i)
	}
	t.api.PrintLog(rai.LVL_MINOR, nil, fmt.Sprintf("Found %d files to replay", t.fileCount))
	prefix := args.String(repargs.prefix.Name)

	t.msgCount = args.Int(repargs.msgCount.Name)
	t.msgsPerSec = args.Int(repargs.perSec.Name)
	t.realtimeSpeed = args.Double(repargs.realtime.Name)

	var timeout int
	if t.msgsPerSec >= 100 || t.realtimeSpeed != 0.0 {
		timeout = 1
		if t.realtimeSpeed != 0.0 {
			t.realtimeSpeed = 1000000000.0 / t.realtimeSpeed /* per nanosecond */
		}
	} else {
		timeout = 100 / t.msgsPerSec
	}
	t.publishOnce = args.Bool(repargs.publishOnce.Name)
	t.printRate = args.Bool(repargs.rate.Name)
	if err := t.openInput(); err != nil {
		return err
	}

	var err error
	if t.session, err = t.api.CreateSession(); err != nil {
		return err
	}
	if err = t.session.Start(); err != nil {
		return err
	}
	if t.pubQueue, err = t.session.CreateQueue(false); err != nil {
		return err
	}
	if t.pub, err = t.session.CreatePublish(false); err != nil {
		return err
	}
	if err = t.pub.SetPrefix(prefix); err != nil {
		return err
	}

	if t.startTimer, err = t.pubQueue.CreateTimer(t, nil); err != nil {
		return err
	}
	t.startTimer.SetInterval(1000) /* delay start 1 second */
	if err = t.startTimer.Start(); err != nil {
		return err
	}

	/* publish messages on a timer, started by startTimer */
	if t.pubTimer, err = t.pubQueue.CreateTimer(t, nil); err != nil {
		return err
	}
	t.pubTimer.SetInterval(int64(timeout))

	if t.printRate {
		if t.printTimer, err = t.pubQueue.CreateTimer(t, nil); err != nil {
			return err
		}
		t.printTimer.SetInterval(1000)
	}
	return nil
}

func (t *raireplay2) dispatchLoop() {
	for !t.quit.Load() {
		if err := t.pubQueue.TimedDispatch(100); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "pubQueue dispatch")
		}
	}
}

func (t *raireplay2) updateClock() int64 {
	now := rai.HiresTimeNanosecs()
	if t.startTime == 0 {
		t.startTime = now
		t.msgsClocked = 0
	} else if now > t.startTime {
		t.msgsClocked = int64(float64(now-t.startTime) / 1000000000.0 * float64(t.msgsPerSec))
	}
	return now
}

/* replay format: "subject\n" "size [delta]\n" packed-message */
func (t *raireplay2) readMsg() (bool, error) {
	size := 0
	haveSubject, haveSize := false, false

	for {
		if !haveSubject {
			for i := t.bufOff; i < t.bufLen; i++ {
				if t.msgBuf[i] == '\n' {
					t.subject = string(t.msgBuf[t.bufOff:i])
					haveSubject = true
					t.bufOff = i + 1
					break
				}
			}
		}
		if haveSubject && !haveSize {
			i := t.bufOff
			for j := t.bufOff; j < t.bufLen; j++ {
				if t.msgBuf[j] == '\n' {
					for ; t.msgBuf[i] >= '0' && t.msgBuf[i] <= '9'; i++ {
						size = size*10 + int(t.msgBuf[i]-'0')
					}
					if size == 0 {
						return false, rai.NewError("Invalid size, not a number")
					}
					haveSize = true
					t.bufOff = j + 1
					c := t.msgBuf[i]
					i++
					if c == ' ' && t.realtimeSpeed != 0.0 {
						fraction, delta := 10.0, 0.0
						for ; t.msgBuf[i] >= '0' && t.msgBuf[i] <= '9'; i++ {
							delta = delta*10 + float64(t.msgBuf[i]-'0')
						}
						c = t.msgBuf[i]
						i++
						if c == '.' {
							for ; t.msgBuf[i] >= '0' && t.msgBuf[i] <= '9'; i++ {
								delta += float64(t.msgBuf[i]-'0') / fraction
								fraction *= 10.0
							}
						}
						t.msgDelta = delta
					}
					break
				}
			}
		}
		if haveSubject && haveSize {
			if t.bufOff+size <= t.bufLen {
				t.msgSize = size
				t.msgOff = t.bufOff
				t.bufOff += size
				return true, nil
			}
		}
		if t.bufOff > 0 {
			copy(t.msgBuf, t.msgBuf[t.bufOff:t.bufLen])
			t.bufLen -= t.bufOff
			t.bufOff = 0
		}
		if t.bufLen == len(t.msgBuf) {
			msgBuf2 := make([]byte, len(t.msgBuf)*2)
			copy(msgBuf2, t.msgBuf)
			t.msgBuf = msgBuf2
		}
		n, err := t.inp.Read(t.msgBuf[t.bufLen:])
		if n > 0 {
			t.bufLen += n
		} else if t.bufLen > 0 {
			return false, rai.NewError("Message truncated at end of file")
		} else {
			if err != nil && err.Error() != "EOF" {
				return false, err
			}
			break
		}
	}
	return false, nil
}

func (t *raireplay2) OnTimer(timer *rai.Timer, cl any) {
	if timer == t.pubTimer {
		t.doPub()
	} else if timer == t.printTimer {
		t.doPrint()
	} else if timer == t.startTimer {
		t.startTimer.Stop()
		err := t.pubTimer.Start()
		if err == nil && t.printTimer != nil {
			err = t.printTimer.Start()
		}
		if err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "Starting timer")
			t.quit.Store(true)
		}
	}
}

func (t *raireplay2) doPub() {
	if err := t.doPubErr(); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Publishing msg")
		t.errCount++
		if t.errCount > 300 {
			t.pubTimer.Stop()
			t.quit.Store(true)
		}
	}
}

func (t *raireplay2) nextMsg() error {
	if t.msgSize == 0 {
		ok, err := t.readMsg()
		if err != nil {
			return err
		}
		if !ok {
			return t.rotateInput()
		}
	}
	return nil
}

func (t *raireplay2) doPubErr() error {
	/* replay at realtime rate */
	if t.realtimeSpeed != 0.0 {
		for {
			if t.quit.Load() {
				return nil
			}
			if err := t.nextMsg(); err != nil {
				return err
			}
			if t.msgSize != 0 {
				if t.msgDelta == 0 { /* if no delta in replay file */
					break
				}
				t.currentTime = rai.HiresTimeNanosecs()
				if float64(t.currentTime-t.baseTime)/t.realtimeSpeed <= t.msgDelta {
					return nil
				}
				if err := t.publishMsg(); err != nil {
					return err
				}
			}
		}
	}
	/* calculate how many messages should be sent at -perSec rate */
	t.currentTime = t.updateClock()
	for t.msgsSent < t.msgsClocked && !t.quit.Load() {
		if err := t.nextMsg(); err != nil {
			return err
		}
		if t.msgSize != 0 {
			if err := t.publishMsg(); err != nil {
				return err
			}
			t.currentTime = rai.HiresTimeNanosecs()
		}
	}
	return nil
}

func (t *raireplay2) publishMsg() error {
	err := t.pub.PublishBuf(t.subject, t.msgBuf[t.msgOff:t.msgOff+t.msgSize],
		rai.HiresTimeToNsTimestamp(t.currentTime))
	if err != nil {
		return err
	}
	t.msgsSent++
	t.bytesSent += int64(t.msgSize)
	t.msgSize = 0
	if t.msgCount != 0 {
		t.msgCount--
		if t.msgCount == 0 {
			t.pubTimer.Stop()
			t.quit.Store(true)
		}
	}
	return nil
}

func (t *raireplay2) rotateInput() error {
	t.closeInput()
	t.fileNum++
	if t.fileNum >= t.fileCount {
		t.fileNum = 0
		if t.publishOnce {
			t.pubTimer.Stop()
			t.quit.Store(true)
			return nil
		}
	}
	return t.openInput()
}

func (t *raireplay2) openInput() error {
	f, err := os.Open(t.files[t.fileNum])
	if err != nil {
		return err
	}
	t.inp = f
	t.baseTime = rai.HiresTimeNanosecs()
	if !t.printRate {
		t.api.PrintLog(rai.LVL_MINOR, nil, "File: "+t.files[t.fileNum])
	}
	return nil
}

func (t *raireplay2) closeInput() {
	if t.inp != nil {
		t.inp.Close()
		t.inp = nil
	}
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

func (t *raireplay2) doPrint() {
	curTime := rai.HiresTimeNanosecs()
	interval := float64(curTime-t.intervalStart) / 1000000000.0
	if interval >= 0.100 {
		t.intervalStart = curTime
		rate := float64(t.msgsSent - t.msgsPrint)
		t.msgsPrint = t.msgsSent
		rate /= interval
		bs := t.bytesSent
		fmt.Printf("msgs=%s/s data=%.1fmbit/s\n", rateFmt(rate),
			float64(bs-t.ivalBytesSent)/1000.0/1000.0*8.0/interval)
		t.ivalBytesSent = bs
	}
}

var replay *raireplay2

func sigHandler(sig int) {
	if replay != nil {
		replay.sigCaught.Store(int32(sig))
		replay.quit.Store(true)
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
	rai.Log(rai.LVL_MINOR, nil, "Finished")
}

func run(api *rai.Api, argv []string) error {
	args, err := rai.NewArgs()
	if err != nil {
		return err
	}
	repargs := newRaireplay2Args()
	if err = api.GetArgs(args); err != nil {
		return err
	}
	repargs.getArgs(args)
	if err = args.AddDefaults(rai.RaiVersion(), "rai_", os.Stderr, "raireplay2"); err != nil {
		return err
	}
	ok, err := args.ProcessArgs(argv)
	if err != nil {
		return err
	}
	if ok {
		replay = newRaireplay2()
		if replay.init(api, args) {
			replay.dispatchLoop()
		}
		if sig := replay.sigCaught.Load(); sig != 0 {
			rai.Log(rai.LVL_MINOR, nil, fmt.Sprintf("Caught signal %d, quitting", sig))
		}
		replay.close()
	}
	return nil
}
