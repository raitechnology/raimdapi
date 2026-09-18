/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 *
 * Package raiapi2 is the Go binding of the Rai API.  It mirrors the Java and
 * .NET bindings (src/raiapi/java, src/raiapi/dotnet) one to one over the flat
 * C api in include/raiapi2_c.h (libraimdapi), so the Java documentation and
 * programs apply.  Naming follows Go conventions: exported methods are
 * CamelCase (CreateSession, GetString, AppendUShort ...), errors are returned
 * instead of thrown, callbacks are interfaces, closures are `any`.
 *
 * Build: the C header is found relative to this directory; libraimdapi.so
 * and the libraries it links must be on the linker path and at run time on
 * LD_LIBRARY_PATH (or use the rpath the GNUmakefile adds), for example:
 *
 *   CGO_LDFLAGS="-L$top/FC43_x86_64/lib64 -Wl,-rpath,$top/FC43_x86_64/lib64" \
 *     go build ./...
 */
package raiapi2

/*
#cgo CFLAGS: -I${SRCDIR}/../../../../include
#cgo LDFLAGS: -lraimdapi
#include <stdlib.h>
#include <string.h>
#include <raiapi2_c.h>
*/
import "C"

import (
	"fmt"
	"unsafe"
)

/* ---- errors --------------------------------------------------------------- */

/* Error is the Rai API error: the module where the status is defined, the
 * status code (its domain is the module) and the reason string.  Errors from
 * the message layer have Module "RaiMsg" / "RaiField" / "RaiDict" (Java
 * RaiMsgException), everything else is the api layer (RaiApiException). */
type Error struct {
	Module string
	Errno  int
	Reason string
	native C.rai_err_t /* nil for Go-side errors */
}

func (e *Error) Error() string {
	if e.native == nil {
		return e.Reason
	}
	return fmt.Sprintf("%s: %s (%d)", e.Module, e.Reason, e.Errno)
}

/* IsMsgError is true when the error came from the message layer */
func (e *Error) IsMsgError() bool {
	return e.Module == "RaiMsg" || e.Module == "RaiField" || e.Module == "RaiDict"
}

/* NewError makes a Go-side api error with a message */
func NewError(msg string) *Error { return &Error{Reason: msg} }

func wrapErr(msg string, cause error) *Error {
	return &Error{Reason: msg + ": " + cause.Error()}
}

/* check converts a rai_err_t into a Go error, nil when ok */
func check(e C.rai_err_t) error {
	if e == nil {
		return nil
	}
	return &Error{
		Module: C.GoString(C.rai_err_module(e)),
		Errno:  int(C.rai_err_status(e)),
		Reason: C.GoString(C.rai_err_reason(e)),
		native: e,
	}
}

/* nativeErr returns the api error record of err, or nil */
func nativeErr(err error) C.rai_err_t {
	if e, ok := err.(*Error); ok {
		return e.native
	}
	return nil
}

/* ---- string helpers ------------------------------------------------------- */

/* cstr allocates a C copy of s; the caller frees it */
func cstr(s string) *C.char { return C.CString(s) }

/* cstrOpt is cstr with "" mapped to NULL (for optional parameters) */
func cstrOpt(s string) *C.char {
	if s == "" {
		return nil
	}
	return C.CString(s)
}

func cfree(p *C.char) {
	if p != nil {
		C.free(unsafe.Pointer(p))
	}
}

/* goStr is C.GoString with NULL -> "" */
func goStr(p *C.char) string {
	if p == nil {
		return ""
	}
	return C.GoString(p)
}

/* goStrN converts a counted string; message strings carry their NUL
 * terminator in the length, strip it (as the Java / .NET bindings do) */
func goStrN(p *C.char, n C.uint32_t) string {
	if p == nil || n == 0 {
		return ""
	}
	s := C.GoStringN(p, C.int(n))
	for len(s) > 0 && s[len(s)-1] == 0 {
		s = s[:len(s)-1]
	}
	return s
}

func goBytes(p unsafe.Pointer, n C.uint32_t) []byte {
	if p == nil || n == 0 {
		return []byte{}
	}
	return C.GoBytes(p, C.int(n))
}

/* cargv builds a NULL-free argv for the C api: argv[0] = program name, as
 * the Java/.NET bindings do, then the arguments */
func cargv(argv []string) (**C.char, C.int, func()) {
	n := len(argv) + 1
	arr := (**C.char)(C.malloc(C.size_t(n) * C.size_t(unsafe.Sizeof(uintptr(0)))))
	sl := unsafe.Slice(arr, n)
	sl[0] = C.CString("go")
	for i, a := range argv {
		sl[i+1] = C.CString(a)
	}
	return arr, C.int(n), func() {
		for _, p := range sl {
			C.free(unsafe.Pointer(p))
		}
		C.free(unsafe.Pointer(arr))
	}
}

func b2i(b bool) C.int {
	if b {
		return 1
	}
	return 0
}
