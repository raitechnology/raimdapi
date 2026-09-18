/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Go port of src/raiapi/java/raipub2.java (and dotnet/raipub2)
 *
 * This example publisher creates a publishing struct.  The struct uses a
 * main loop to publish a message up to count times. */
package main

import (
	"fmt"
	"os"
	"strings"
	"sync/atomic"
	"time"

	rai "github.com/injinj/raimdapi/src/raiapi/golang/raiapi2"
)

type raipub2Args struct {
	subject, prefix, data, form, msgtype, recstat rai.StringArg
	count                                         rai.IntArg
	sass                                          rai.BoolArg
}

func newRaipub2Args() *raipub2Args {
	return &raipub2Args{
		subject: rai.NewStringArg("subject", "TEST.REC.AAA.NaE", "<subject>", "Subject name to publsh"),
		prefix: rai.NewStringArg("prefix", "", "<subject>",
			"Subject to prefix publish subject with, usually set to _TIC. if using SASS/RV"),
		data: rai.NewStringArg("data", "ASK=11.0,BID=10.5", "<field=val,...>", "Field values to publish"),
		form: rai.NewStringArg("form", "EQ", "<name>", "Form class to use, for none use \"\""),
		msgtype: rai.NewStringArg("msgType", "INITIAL", "<msg-type>",
			"Type of message to Publish, for example: UPDATE, VERIFY, DROP"),
		recstat: rai.NewStringArg("recStatus", "OK", "<rec-status>",
			"Status message to Publish, for example: OK, EXPIRED"),
		count: rai.NewIntArg("count", 1, "<num>", "Number of times to publish message"),
		sass:  rai.NewBoolArg("sass", false, "<bool>", "Use QFORMs instead of RaiMsg message format"),
	}
}

func (a *raipub2Args) getArgs(args *rai.Args) {
	args.Add(a.subject)
	args.Add(a.prefix)
	args.Add(a.data)
	args.Add(a.form)
	args.Add(a.msgtype)
	args.Add(a.recstat)
	args.Add(a.count)
	args.Add(a.sass)
}

type raipub2 struct {
	api                                            *rai.Api
	session                                        *rai.Session
	pub                                            *rai.Publish
	dataDict                                       *rai.Dict
	queue                                          *rai.Queue
	subjname, formname, typenam, recstat, datavals string
	counter                                        int
	seqNo                                          int16
	sigCaught                                      atomic.Int32
	useSass                                        bool
	quit                                           atomic.Bool
}

func newRaipub2() *raipub2 { return &raipub2{seqNo: 1} }

func (t *raipub2) pubMsg() {
	if err := t.pubMsgErr(); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Publishing message")
	}
}

func (t *raipub2) pubMsgErr() error {
	msgType, recStatus := rai.INITIAL, rai.STATUS_OK
	/* Check what sort of message we want to use.  If we are packing a SASS
	 * compatible QForm use the appropriate message prototype */
	if t.typenam != "" {
		msgType = rai.SassStringToMsgType(t.typenam)
		t.api.PrintLog(rai.LVL_DEBUG, nil, fmt.Sprintf("MSG_TYPE %s=%d", t.typenam, msgType))
	}
	if t.recstat != "" {
		recStatus = rai.SassStringToRecStatus(t.recstat)
		t.api.PrintLog(rai.LVL_DEBUG, nil, fmt.Sprintf("REC_STATUS %s=%d", t.recstat, recStatus))
	}
	var raiMsg *rai.Msg
	var err error
	if t.useSass {
		raiMsg, err = rai.NewSASSMsgForm(msgType, t.formname, t.seqNo, recStatus)
	} else {
		raiMsg, err = rai.NewRaiMsgForm(msgType, t.formname, t.seqNo, recStatus)
	}
	if err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Creating message formclass = "+t.formname)
		return err
	}
	defer raiMsg.Delete()
	t.seqNo++

	/* Take the data values provided on the command line and add them to the
	 * message: field=value,field=value */
	for _, kv := range strings.Split(t.datavals, ",") {
		eq := strings.IndexByte(kv, '=')
		if eq < 0 {
			break
		}
		fname, fval := kv[:eq], kv[eq+1:]
		/* the message is not using a pre-defined record type so any field
		 * can be specified */
		t.api.PrintLog(rai.LVL_DEBUG, nil, "Setting field "+fname+"="+fval)
		if err = raiMsg.Append(fname, fval); err != nil {
			return err
		}
	}
	if err = t.pub.PublishMsg(t.subjname, raiMsg, 0); err != nil {
		return err
	}
	t.api.PrintLog(rai.LVL_MINOR, nil, "Published "+t.pub.GetPrefix()+t.subjname)
	return nil
}

func (t *raipub2) init(api *rai.Api, args *rai.Args) bool {
	t.api = api
	if err := t.initErr(args); err != nil {
		t.api.PrintLog(rai.LVL_ERROR, err, "Not initialized, stopped")
		return false
	}
	return true
}

func (t *raipub2) initErr(args *rai.Args) error {
	pubargs := newRaipub2Args()
	t.subjname = args.String(pubargs.subject.Name)
	t.formname = args.String(pubargs.form.Name)
	t.typenam = args.String(pubargs.msgtype.Name)
	t.recstat = args.String(pubargs.recstat.Name)
	t.datavals = args.String(pubargs.data.Name)
	t.counter = args.Int(pubargs.count.Name)
	t.useSass = args.Bool(pubargs.sass.Name)
	noDictionary := !t.useSass

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

	var err error
	if t.session, err = t.api.CreateSession(); err != nil {
		return err
	}
	if err = t.session.Start(); err != nil {
		return err
	}
	if t.queue, err = t.session.CreateQueue(false); err != nil {
		return err
	}
	if t.pub, err = t.session.CreatePublish(false); err != nil {
		return err
	}
	if err = t.pub.SetPrefix(args.String(pubargs.prefix.Name)); err != nil {
		return err
	}

	/* If we are using the SASS protocol for publishing we need to create and
	 * load a dictionary. */
	if t.useSass && !noDictionary {
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
			t.api.PrintLog(rai.LVL_MINOR, nil, "Dictionary load timed out")
			return rai.NewError("Dictionary load timed out")
		}
		t.api.PrintLog(rai.LVL_MINOR, nil, "Dictionary received")
	}
	return nil
}

func (t *raipub2) close() {
	t.quit.Store(true)
	if t.pub != nil {
		t.pub.Destroy()
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
}

func (t *raipub2) dispatchLoop() {
	for !t.quit.Load() {
		if err := t.queue.TimedDispatch(100); err != nil {
			t.api.PrintLog(rai.LVL_ERROR, err, "In dispatchLoop")
			continue
		}
		t.pubMsg()
		t.counter--
		if t.counter <= 0 {
			break
		}
	}
}

var pubTest *raipub2

func sigHandler(sig int) {
	if pubTest != nil {
		pubTest.sigCaught.Store(int32(sig))
		pubTest.quit.Store(true)
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
	pubargs := newRaipub2Args()
	/* get the api's configuration arguments */
	if err = api.GetArgs(args); err != nil {
		return err
	}
	/* get the publish args */
	pubargs.getArgs(args)
	/* get the arguments for the dictionary */
	if err = rai.GetDictArgs(args); err != nil {
		return err
	}
	/* get the logging, version, help, rc arguments and sets error output */
	if err = args.AddDefaults(rai.RaiVersion(), "rai_", os.Stderr, "raipub2"); err != nil {
		return err
	}
	ok, err := args.ProcessArgs(argv)
	if err != nil {
		return err
	}
	if ok {
		pubTest = newRaipub2()
		if pubTest.init(api, args) {
			pubTest.dispatchLoop()
		}
		if sig := pubTest.sigCaught.Load(); sig != 0 {
			rai.Log(rai.LVL_MINOR, nil, fmt.Sprintf("Caught signal %d, quitting", sig))
		}
		pubTest.close()
	}
	return nil
}
