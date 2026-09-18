/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Mirrors com.rai.raiapi2.Args, StringArg, BoolArg, IntArg, DoubleArg,
 * Time and TimeRotate. */
package raiapi2

/*
#include <stdlib.h>
#include <raiapi2_c.h>

uint32_t rai_go_write_fn( void *cl,  const uint8_t *buf,  uint32_t len );
*/
import "C"

import (
	"io"
	"unsafe"
)

/* ---- Args --------------------------------------------------------------- */

const (
	IGNORE_ARG     = 0
	RESOURCE_ARG   = 1
	COMMAND_ARG    = 2
	TIME_SEC_ARG   = 4
	TIME_MS_ARG    = 8
	MEM_ARG        = 16
	HELP_ARG       = 32
	VERSION_ARG    = 64
	PRINTRC_ARG    = 128
	RCFILE_ARG     = 256
	LIST_ARG       = 512
	NO_DEFAULT_VAL = 1024
	BITS_ARG       = 2048
)

/* Arg is one of StringArg, BoolArg, IntArg, DoubleArg */
type Arg interface {
	add(a *Args, flags int) error
	ArgName() string
}

/* StringArg: Def "" means no default (null in Java) */
type StringArg struct{ Name, Def, Example, Description string }
type BoolArg struct {
	Name                 string
	Def                  bool
	Example, Description string
}
type IntArg struct {
	Name                 string
	Def                  int
	Example, Description string
}
type DoubleArg struct {
	Name                 string
	Def                  float64
	Example, Description string
}

func (a StringArg) ArgName() string { return a.Name }
func (a BoolArg) ArgName() string   { return a.Name }
func (a IntArg) ArgName() string    { return a.Name }
func (a DoubleArg) ArgName() string { return a.Name }

func (a StringArg) add(args *Args, flags int) error {
	cn, cd, ce, cs := cstr(a.Name), cstrOpt(a.Def), cstrOpt(a.Example), cstrOpt(a.Description)
	defer cfree(cn)
	defer cfree(cd)
	defer cfree(ce)
	defer cfree(cs)
	return check(C.rai_args_add_string(args.args, cn, cd, ce, cs, C.int(flags)))
}
func (a BoolArg) add(args *Args, flags int) error {
	cn, ce, cs := cstr(a.Name), cstrOpt(a.Example), cstrOpt(a.Description)
	defer cfree(cn)
	defer cfree(ce)
	defer cfree(cs)
	return check(C.rai_args_add_bool(args.args, cn, b2i(a.Def), ce, cs, C.int(flags)))
}
func (a IntArg) add(args *Args, flags int) error {
	cn, ce, cs := cstr(a.Name), cstrOpt(a.Example), cstrOpt(a.Description)
	defer cfree(cn)
	defer cfree(ce)
	defer cfree(cs)
	return check(C.rai_args_add_int(args.args, cn, C.uint32_t(a.Def), ce, cs, C.int(flags)))
}
func (a DoubleArg) add(args *Args, flags int) error {
	cn, ce, cs := cstr(a.Name), cstrOpt(a.Example), cstrOpt(a.Description)
	defer cfree(cn)
	defer cfree(ce)
	defer cfree(cs)
	return check(C.rai_args_add_double(args.args, cn, C.double(a.Def), ce, cs, C.int(flags)))
}

/* Args is command line / rc file / environment argument processing.  Add the
 * args with defaults, then ProcessArgs(argv) populates the values, -help
 * prints them. */
type Args struct {
	args C.rai_args_t
	out  *writeAdapter
}

func NewArgs() (*Args, error) {
	a := C.rai_args_create()
	if a == nil {
		return nil, NewError("unable to create args")
	}
	return &Args{args: a}, nil
}

/* Delete releases the native args */
func (a *Args) Delete() {
	if a.args != nil {
		C.rai_args_delete(a.args)
		a.args = nil
	}
	if a.out != nil {
		a.out.release()
		a.out = nil
	}
}

/* Add an argument; flags default to COMMAND_ARG | RESOURCE_ARG */
func (a *Args) Add(arg Arg, flags ...int) error {
	f := COMMAND_ARG | RESOURCE_ARG
	if len(flags) > 0 {
		f = flags[0]
	}
	return arg.add(a, f)
}

/* AddDefaults adds -log, -logLevel, -logVerb, -help, -version, -rcFile,
 * -printRC; help and version output is written to out (usually os.Stderr,
 * nil = stderr) */
func (a *Args) AddDefaults(vers, prefix string, out io.Writer, argv0 string) error {
	if a.out == nil && out != nil {
		a.out = newWriter(out)
	}
	cv, cp, c0 := cstr(vers), cstr(prefix), cstr(argv0)
	defer cfree(cv)
	defer cfree(cp)
	defer cfree(c0)
	var fn C.rai_write_fn
	var cl unsafe.Pointer
	if a.out != nil {
		fn = C.rai_write_fn(C.rai_go_write_fn)
		cl = a.out.ptr()
	}
	return check(C.rai_args_add_defaults(a.args, cv, cp, fn, cl, c0))
}

/* ProcessArgs parses argv (os.Args[1:]); false when -help or -version was
 * handled and the program should exit */
func (a *Args) ProcessArgs(argv []string) (bool, error) {
	av, n, free := cargv(argv)
	defer free()
	var ok C.int
	err := check(C.rai_args_process(a.args, n, av, &ok))
	return ok != 0, err
}

func (a *Args) GetNumValues(n string) int {
	cn := cstr(n)
	defer cfree(cn)
	return int(C.rai_args_num_values(a.args, cn))
}

/* GetString returns value num of n ("" when unset / null) */
func (a *Args) GetString(n string, num ...int) (string, error) {
	cn := cstr(n)
	defer cfree(cn)
	var s *C.char
	err := check(C.rai_args_get_string(a.args, cn, C.uint32_t(idx(num)), &s))
	return goStr(s), err
}
func (a *Args) GetBool(n string, num ...int) (bool, error) {
	cn := cstr(n)
	defer cfree(cn)
	var v C.int
	err := check(C.rai_args_get_bool(a.args, cn, C.uint32_t(idx(num)), &v))
	return v != 0, err
}
func (a *Args) GetInt(n string, num ...int) (int, error) {
	cn := cstr(n)
	defer cfree(cn)
	var v C.uint32_t
	err := check(C.rai_args_get_int(a.args, cn, C.uint32_t(idx(num)), &v))
	return int(int32(v)), err
}
func (a *Args) GetDouble(n string, num ...int) (float64, error) {
	cn := cstr(n)
	defer cfree(cn)
	var v C.double
	err := check(C.rai_args_get_double(a.args, cn, C.uint32_t(idx(num)), &v))
	return float64(v), err
}
func idx(num []int) int {
	if len(num) > 0 {
		return num[0]
	}
	return 0
}

/* String / Bool / Int / Double are the getters without the error: a missing
 * arg name is a programming error, so they return the zero value */
func (a *Args) String(n string, num ...int) string  { s, _ := a.GetString(n, num...); return s }
func (a *Args) Bool(n string, num ...int) bool      { b, _ := a.GetBool(n, num...); return b }
func (a *Args) Int(n string, num ...int) int        { i, _ := a.GetInt(n, num...); return i }
func (a *Args) Double(n string, num ...int) float64 { d, _ := a.GetDouble(n, num...); return d }

func (a *Args) SetString(n, val string) error {
	cn, cv := cstr(n), cstr(val)
	defer cfree(cn)
	defer cfree(cv)
	return check(C.rai_args_set_string(a.args, cn, cv))
}
func (a *Args) SetBool(n string, val bool) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_args_set_bool(a.args, cn, b2i(val)))
}
func (a *Args) SetInt(n string, val int) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_args_set_int(a.args, cn, C.uint32_t(val)))
}
func (a *Args) SetDouble(n string, val float64) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_args_set_double(a.args, cn, C.double(val)))
}
func (a *Args) IsSet(n string) bool {
	cn := cstr(n)
	defer cfree(cn)
	return C.rai_args_is_set(a.args, cn) != 0
}

/* ---- Time --------------------------------------------------------------- */

const (
	TZ_LOCAL_TIME = 0
	TZ_GM_TIME    = 1
)

/* CurrentTimeNanosecs is the wall clock in nanoseconds since the epoch */
func CurrentTimeNanosecs() int64 { return int64(C.rai_time_current_ns()) }

/* CurrentTimeMillis is the wall clock in ms (java System.currentTimeMillis) */
func CurrentTimeMillis() int64 { return CurrentTimeNanosecs() / 1000000 }

/* NsTimestamp formats a ns timestamp with precision fractional digits, 0 = now */
func NsTimestamp(ns int64, precision int) string {
	var buf [80]C.char
	return goStr(C.rai_time_ns_timestamp(C.int64_t(ns), C.int(precision), &buf[0], 80))
}
func UsTimestamp(us int64, precision int) string { return NsTimestamp(us*1000, precision) }
func MsTimestamp(ms int64, precision int) string { return NsTimestamp(ms*1000000, precision) }

/* NsIntervalTime formats a ns interval as "1.5s", "20ms" ... */
func NsIntervalTime(ns int64) string {
	var buf [80]C.char
	return goStr(C.rai_time_ns_interval(C.int64_t(ns), &buf[0], 80))
}
func UsIntervalTime(us int64) string { return NsIntervalTime(us * 1000) }
func MsIntervalTime(ms int64) string { return NsIntervalTime(ms * 1000000) }

/* HiresTimeNanosecs is the high resolution monotonic clock, in ns */
func HiresTimeNanosecs() int64 { return int64(C.rai_time_hires_ns()) }

/* HiresTimeToNsTimestamp converts a hires time to a wall clock ns timestamp */
func HiresTimeToNsTimestamp(h int64) int64 { return int64(C.rai_time_hires_to_ns(C.int64_t(h))) }

/* Strftime formats a ms timestamp in local or GM time */
func Strftime(tz int, ms int64, format string) string {
	var buf [256]C.char
	cf := cstr(format)
	defer cfree(cf)
	return goStr(C.rai_time_strftime(C.int(tz), C.int64_t(ms), cf, &buf[0], 256))
}

/* ---- TimeRotate --------------------------------------------------------- */

const (
	ROTATE_UNSPECIFIED = 0
	ROTATE_DAILY       = 1
	ROTATE_WEEKLY      = 2
	MSECS_IN_SEC       = int64(1000)
	MSECS_IN_DAY       = MSECS_IN_SEC * 24 * 60 * 60
	MSECS_IN_WEEK      = 7 * MSECS_IN_DAY
)

/* TimeRotate is a file rotation schedule: at a time of day / week or on a
 * period */
type TimeRotate struct {
	st C.rai_time_rotate_t
}

func NewTimeRotate() *TimeRotate           { return &TimeRotate{} }
func (r *TimeRotate) Init()                { r.st = C.rai_time_rotate_t{} }
func (r *TimeRotate) Time() int64          { return int64(r.st.time) }
func (r *TimeRotate) Period() int64        { return int64(r.st.period) }
func (r *TimeRotate) LastTime() int64      { return int64(r.st.last_time) }
func (r *TimeRotate) DayOrWeek() int       { return int(r.st.day_or_week) }
func (r *TimeRotate) SetLastTime(lt int64) { r.st.last_time = C.int64_t(lt) }

/* SetRotateTime parses a rotate time spec, for example "00:00" (daily) or
 * "Sun 00:00"; "" clears */
func (r *TimeRotate) SetRotateTime(timeSpec string, rotDorW int, rotTime int64) bool {
	cs := cstrOpt(timeSpec)
	defer cfree(cs)
	return C.rai_time_rotate_set_time(&r.st, cs, C.int(rotDorW), C.int64_t(rotTime)) != 0
}

/* SetRotatePeriod parses a period spec ("1 hour") or uses rotatePeriod ms
 * when spec is "" */
func (r *TimeRotate) SetRotatePeriod(periodSpec string, rotatePeriod int64) bool {
	cs := cstrOpt(periodSpec)
	defer cfree(cs)
	return C.rai_time_rotate_set_period(&r.st, cs, C.int64_t(rotatePeriod)) != 0
}

func (r *TimeRotate) per() int64 {
	if r.st.period != 0 {
		return int64(r.st.period)
	}
	if r.st.day_or_week == ROTATE_WEEKLY {
		return MSECS_IN_WEEK
	}
	if r.st.day_or_week == ROTATE_DAILY {
		return MSECS_IN_DAY
	}
	return 0
}

/* CheckRotate is true when the rotate time has passed; advances time by the
 * period */
func (r *TimeRotate) CheckRotate() bool {
	doRotate := false
	if r.st.time != 0 {
		per := r.per()
		if per != 0 {
			currTime := CurrentTimeMillis()
			t := int64(r.st.time)
			/* lastTime check is to rotate from a previous run */
			if t > int64(r.st.last_time) || t+per <= currTime {
				doRotate = true
				for t > currTime {
					t -= per
				}
				for t+per <= currTime {
					t += per
				}
				r.st.time = C.int64_t(t)
			}
			r.st.last_time = C.int64_t(currTime)
		}
	}
	return doRotate
}

/* NextRotate returns (next rotate time ms, ms until then), zeros when not
 * rotating */
func (r *TimeRotate) NextRotate() (nextTime, diffTime int64) {
	if r.st.time != 0 {
		nextTime = r.per()
		if nextTime != 0 {
			currTime := CurrentTimeMillis()
			if int64(r.st.time) > int64(r.st.last_time) {
				nextTime = currTime
			} else {
				nextTime += int64(r.st.time)
				if nextTime < currTime {
					nextTime = currTime
				}
			}
			diffTime = nextTime - currTime
		}
	}
	return
}

func (r *TimeRotate) GetInterval() int64 {
	if r.st.time == 0 {
		return 0
	}
	return r.per()
}

/* constructors in the Java argument order (name, default, example, description) */
func NewStringArg(name, def, example, description string) StringArg {
	return StringArg{name, def, example, description}
}
func NewBoolArg(name string, def bool, example, description string) BoolArg {
	return BoolArg{name, def, example, description}
}
func NewIntArg(name string, def int, example, description string) IntArg {
	return IntArg{name, def, example, description}
}
func NewDoubleArg(name string, def float64, example, description string) DoubleArg {
	return DoubleArg{name, def, example, description}
}
