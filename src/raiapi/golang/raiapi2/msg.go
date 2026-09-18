/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Mirrors com.rai.raimsg.RaiMsg / RaiField / Partial. */
package raiapi2

/*
#include <stdlib.h>
#include <raiapi2_c.h>

uint32_t rai_go_write_fn( void *cl,  const uint8_t *buf,  uint32_t len );
*/
import "C"

import (
	"fmt"
	"io"
	"strings"
	"unsafe"
)

/* field types */
const (
	RAIMSG_NODATA   = 0
	RAIMSG_MESSAGE  = 1
	RAIMSG_STRING   = 2
	RAIMSG_OPAQUE   = 3
	RAIMSG_BOOLEAN  = 4
	RAIMSG_INT      = 5
	RAIMSG_UINT     = 6
	RAIMSG_REAL     = 7
	RAIMSG_ARRAY    = 8
	RAIMSG_PARTIAL  = 9
	RAIMSG_IPDATA   = 10
	RAIMSG_MAXVALID = 11
)

/* protocols */
const (
	RAIMSG_PROTO             = 0 /* tibmsg self describing */
	RV_SASS_PROTO            = 1
	TIB_SASS_PROTO           = 2 /* sass with tib header */
	TIB_SASS_FORM_PROTO      = 3 /* sass with fixed field offset */
	RV_RAIMSG_PROTO          = 4
	XREP_PROTO               = 5
	RV_PROTO                 = 6 /* rv self describing */
	CISERVER_SASS_PROTO      = 7
	CISERVER_SASS_FORM_PROTO = 8
)

/* common error codes (Error.Errno with IsMsgError()) */
const (
	BAD_ARG          = 1
	BAD_MAGIC_NUMBER = 2
	NOT_FOUND        = 9
	NO_FIELD         = 11
	BAD_CVT_STRING   = 43
	BAD_CVT_BOOL     = 44
	BAD_CVT_INT      = 45
	BAD_CVT_REAL     = 46
)

/* PrintXML attribute flags */
const (
	ADD_TYPE_ATTR         = 0x1
	ADD_SIZE_ATTR         = 0x2
	ADD_FID_ATTR          = 0x4
	ADD_PARTIAL_OFF_ATTR  = 0x8
	ADD_ARRAY_COUNT_ATTR  = 0x10
	ADD_ARRAY_TYPE_ATTR   = 0x20
	ADD_ARRAY_ELSIZE_ATTR = 0x40
	ADD_HINT_ATTR         = 0x80
	ADD_ALL_ATTRS         = 0x3ff
)

/* IsNotFound is true for the message layer's field-not-found error */
func IsNotFound(err error) bool {
	e, ok := err.(*Error)
	return ok && e.IsMsgError() && e.Errno == NOT_FOUND
}

/* Partial is a partial field: opaque data at an offset within the field */
type Partial struct {
	Data   []byte
	Offset int
}

func (p Partial) String() string { return string(p.Data) }

/* Msg is a self describing message with named, typed fields, which can pack
 * into several wire protocols (RAIMSG, TIB_SASS, RV, ...).  A message handed
 * to a callback is owned by the api and only valid during the callback. */
type Msg struct {
	msg   C.rai_msg_t
	owned bool
}

func RaiMsgVersion() string { return goStr(C.rai_msg_version()) }

/* NewMsg creates an empty message using the protocol (RAIMSG_PROTO ...) */
func NewMsg(proto int) (*Msg, error) {
	var m C.rai_msg_t
	if err := check(C.rai_msg_create(C.int(proto), &m)); err != nil {
		return nil, err
	}
	return &Msg{msg: m, owned: true}, nil
}

/* Delete releases an owned message; callback messages are invalidated by the
 * binding after the callback returns */
func (m *Msg) Delete() {
	if m.msg != nil && m.owned {
		C.rai_msg_delete(m.msg)
	}
	m.msg = nil
}
func (m *Msg) invalidate() { m.msg = nil }

func (m *Msg) ReUse(proto int) error { return check(C.rai_msg_reuse(m.msg, C.int(proto))) }
func (m *Msg) ReUseSame() error      { return check(C.rai_msg_reuse(m.msg, -1)) }
func (m *Msg) SetProtocol(proto int) { C.rai_msg_set_protocol(m.msg, C.int(proto)) }
func (m *Msg) GetProtocol() int      { return int(C.rai_msg_get_protocol(m.msg)) }
func (m *Msg) GetProtocolString() string {
	return goStr(C.rai_msg_get_protocol_string(m.msg))
}

/* SASS header helpers */
func MsgTypeToString(msgType int16) string {
	var b [32]C.char
	return goStr(C.rai_msg_type_to_string(C.uint16_t(msgType), &b[0], 32))
}
func StringToMsgType(s string) int16 {
	cs := cstr(s)
	defer cfree(cs)
	return int16(C.rai_msg_string_to_type(cs))
}
func RecStatusToString(recStatus int16) string {
	var b [32]C.char
	return goStr(C.rai_rec_status_to_string(C.uint16_t(recStatus), &b[0], 32))
}
func StringToRecStatus(s string) int16 {
	cs := cstr(s)
	defer cfree(cs)
	return int16(C.rai_string_to_rec_status(cs))
}
func RecTypeToString(recType int16) (string, error) {
	var s *C.char
	err := check(C.rai_msg_rec_type_to_string(C.uint16_t(recType), &s))
	return goStr(s), err
}
func StringToRecType(s string) (int16, error) {
	cs := cstr(s)
	defer cfree(cs)
	var r C.uint16_t
	err := check(C.rai_msg_string_to_rec_type(cs, &r))
	return int16(r), err
}
func (m *Msg) hdrString(f string) (string, error) {
	cf := cstr(f)
	defer cfree(cf)
	var s *C.char
	err := check(C.rai_msg_get_hdr_string(m.msg, cf, &s))
	return goStr(s), err
}
func (m *Msg) setHdrString(f, v string) error {
	cf, cv := cstr(f), cstr(v)
	defer cfree(cf)
	defer cfree(cv)
	return check(C.rai_msg_set_hdr_string(m.msg, cf, cv))
}
func (m *Msg) GetMsgTypeString() (string, error)   { return m.hdrString("MSG_TYPE") }
func (m *Msg) SetMsgTypeString(s string) error     { return m.setHdrString("MSG_TYPE", s) }
func (m *Msg) GetRecTypeString() (string, error)   { return m.hdrString("REC_TYPE") }
func (m *Msg) SetRecTypeString(s string) error     { return m.setHdrString("REC_TYPE", s) }
func (m *Msg) GetRecStatusString() (string, error) { return m.hdrString("REC_STATUS") }
func (m *Msg) SetRecStatusString(s string) error   { return m.setHdrString("REC_STATUS", s) }

func (m *Msg) ClearForm() error     { return check(C.rai_msg_clear_form(m.msg)) }
func (m *Msg) Release() error       { return check(C.rai_msg_release(m.msg)) }
func (m *Msg) Copy(from *Msg) error { return check(C.rai_msg_copy(m.msg, from.msg)) }

/* ---- typed getters, NOT_FOUND error when missing --------------------------- */

/* Get returns a field value as the natural Go type: bool, int8..int64,
 * uint8..uint64, float32, float64, string, []byte (opaque), Partial, *Msg
 * (sub message, owned by the caller) or a slice of those */
func (m *Msg) Get(name string) (any, error) {
	f, err := NewField()
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	found, err := f.Find(m, name)
	if err != nil {
		return nil, err
	}
	if !found {
		return nil, notFound(name)
	}
	return f.Get()
}

func notFound(name string) error {
	return &Error{Module: "RaiMsg", Errno: NOT_FOUND, Reason: "field not found: " + name}
}

func (m *Msg) GetBool(name string) (bool, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.int
	err := check(C.rai_msg_get_bool(m.msg, cn, &v))
	return v != 0, err
}
func (m *Msg) GetByte(name string) (int8, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.int8_t
	err := check(C.rai_msg_get_i8(m.msg, cn, &v))
	return int8(v), err
}
func (m *Msg) GetShort(name string) (int16, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.int16_t
	err := check(C.rai_msg_get_i16(m.msg, cn, &v))
	return int16(v), err
}
func (m *Msg) GetInt(name string) (int32, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.int32_t
	err := check(C.rai_msg_get_i32(m.msg, cn, &v))
	return int32(v), err
}
func (m *Msg) GetLong(name string) (int64, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.int64_t
	err := check(C.rai_msg_get_i64(m.msg, cn, &v))
	return int64(v), err
}
func (m *Msg) GetFloat(name string) (float32, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.float
	err := check(C.rai_msg_get_f32(m.msg, cn, &v))
	return float32(v), err
}
func (m *Msg) GetDouble(name string) (float64, error) {
	cn := cstr(name)
	defer cfree(cn)
	var v C.double
	err := check(C.rai_msg_get_f64(m.msg, cn, &v))
	return float64(v), err
}
func (m *Msg) GetString(name string) (string, error) {
	cn := cstr(name)
	defer cfree(cn)
	var s *C.char
	var n C.uint32_t
	err := check(C.rai_msg_get_string(m.msg, cn, &s, &n))
	return goStrN(s, n), err
}
func (m *Msg) GetOpaque(name string) ([]byte, error) {
	cn := cstr(name)
	defer cfree(cn)
	var p unsafe.Pointer
	var n C.uint32_t
	err := check(C.rai_msg_get_opaque(m.msg, cn, &p, &n))
	return goBytes(p, n), err
}
func (m *Msg) GetPartial(name string) (Partial, error) {
	f, err := m.fld(name)
	if err != nil {
		return Partial{}, err
	}
	defer f.Delete()
	return f.GetPartial(), nil
}
func (m *Msg) GetBoolArray(name string) ([]bool, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetBoolArray()
}
func (m *Msg) GetByteArray(name string) ([]int8, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetByteArray()
}
func (m *Msg) GetShortArray(name string) ([]int16, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetShortArray()
}
func (m *Msg) GetIntArray(name string) ([]int32, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetIntArray()
}
func (m *Msg) GetLongArray(name string) ([]int64, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetLongArray()
}
func (m *Msg) GetFloatArray(name string) ([]float32, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetFloatArray()
}
func (m *Msg) GetDoubleArray(name string) ([]float64, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetDoubleArray()
}
func (m *Msg) GetStringArray(name string) ([]string, error) {
	f, err := m.fld(name)
	if err != nil {
		return nil, err
	}
	defer f.Delete()
	return f.GetStringArray()
}
func (m *Msg) fld(name string) (*Field, error) {
	f, err := NewField()
	if err != nil {
		return nil, err
	}
	found, err := f.Find(m, name)
	if err != nil || !found {
		f.Delete()
		if err == nil {
			err = notFound(name)
		}
		return nil, err
	}
	return f, nil
}

/* ---- append / update -------------------------------------------------------- */

/* Append a field, choosing the type from the value: bool, int8..int64,
 * uint8..uint64, int, uint, float32, float64, string, []byte, Partial, *Msg */
func (m *Msg) Append(name string, val any) error         { return m.put(name, val, false, false) }
func (m *Msg) AppendUnsigned(name string, val any) error { return m.put(name, val, false, true) }
func (m *Msg) Update(name string, val any) error         { return m.put(name, val, true, false) }
func (m *Msg) UpdateUnsigned(name string, val any) error { return m.put(name, val, true, true) }

func (m *Msg) put(n string, val any, update, unsigned bool) error {
	if val == nil {
		return NewError("nil value for field " + n)
	}
	u := b2i(unsigned)
	cn := cstr(n)
	defer cfree(cn)
	var e C.rai_err_t
	switch v := val.(type) {
	case bool:
		if update {
			e = C.rai_msg_update_bool(m.msg, cn, b2i(v))
		} else {
			e = C.rai_msg_append_bool(m.msg, cn, b2i(v))
		}
	case int8:
		e = m.putI8(cn, C.int8_t(v), u, update)
	case uint8:
		e = m.putI8(cn, C.int8_t(v), 1, update)
	case int16:
		e = m.putI16(cn, C.int16_t(v), u, update)
	case uint16:
		e = m.putI16(cn, C.int16_t(v), 1, update)
	case int32:
		e = m.putI32(cn, C.int32_t(v), u, update)
	case uint32:
		e = m.putI32(cn, C.int32_t(v), 1, update)
	case int:
		e = m.putI64(cn, C.int64_t(v), u, update)
	case uint:
		e = m.putI64(cn, C.int64_t(v), 1, update)
	case int64:
		e = m.putI64(cn, C.int64_t(v), u, update)
	case uint64:
		e = m.putI64(cn, C.int64_t(v), 1, update)
	case float32:
		if update {
			e = C.rai_msg_update_f32(m.msg, cn, C.float(v))
		} else {
			e = C.rai_msg_append_f32(m.msg, cn, C.float(v))
		}
	case float64:
		if update {
			e = C.rai_msg_update_f64(m.msg, cn, C.double(v))
		} else {
			e = C.rai_msg_append_f64(m.msg, cn, C.double(v))
		}
	case string:
		cv := cstr(v)
		defer cfree(cv)
		if update {
			e = C.rai_msg_update_string(m.msg, cn, cv)
		} else {
			e = C.rai_msg_append_string(m.msg, cn, cv)
		}
	case []byte:
		if update {
			return m.UpdateOpaque(n, v)
		}
		return m.AppendOpaque(n, v)
	case Partial:
		if update {
			return NewError("update partial not supported")
		}
		return m.AppendPartial(n, v)
	case *Msg:
		if update {
			return NewError("update sub message not supported")
		}
		return m.AppendMsg(n, v)
	default:
		return NewError(fmt.Sprintf("unsupported field type %T for %s", val, n))
	}
	return check(e)
}
func (m *Msg) putI8(cn *C.char, v C.int8_t, u C.int, update bool) C.rai_err_t {
	if update {
		return C.rai_msg_update_i8(m.msg, cn, v, u)
	}
	return C.rai_msg_append_i8(m.msg, cn, v, u)
}
func (m *Msg) putI16(cn *C.char, v C.int16_t, u C.int, update bool) C.rai_err_t {
	if update {
		return C.rai_msg_update_i16(m.msg, cn, v, u)
	}
	return C.rai_msg_append_i16(m.msg, cn, v, u)
}
func (m *Msg) putI32(cn *C.char, v C.int32_t, u C.int, update bool) C.rai_err_t {
	if update {
		return C.rai_msg_update_i32(m.msg, cn, v, u)
	}
	return C.rai_msg_append_i32(m.msg, cn, v, u)
}
func (m *Msg) putI64(cn *C.char, v C.int64_t, u C.int, update bool) C.rai_err_t {
	if update {
		return C.rai_msg_update_i64(m.msg, cn, v, u)
	}
	return C.rai_msg_append_i64(m.msg, cn, v, u)
}

func bufPtr(b []byte) unsafe.Pointer {
	if len(b) == 0 {
		return nil
	}
	return unsafe.Pointer(&b[0])
}

func (m *Msg) AppendOpaque(n string, buf []byte) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_msg_append_opaque(m.msg, cn, bufPtr(buf), C.uint32_t(len(buf))))
}
func (m *Msg) UpdateOpaque(n string, buf []byte) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_msg_update_opaque(m.msg, cn, bufPtr(buf), C.uint32_t(len(buf))))
}
func (m *Msg) AppendPartial(n string, p Partial) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_msg_append_partial(m.msg, cn, bufPtr(p.Data), C.uint32_t(len(p.Data)), C.uint32_t(p.Offset)))
}
func (m *Msg) AppendMsg(n string, sub *Msg) error {
	cn := cstr(n)
	defer cfree(cn)
	return check(C.rai_msg_append_msg(m.msg, cn, sub.msg))
}
func (m *Msg) AppendField(f *Field) error { return check(C.rai_msg_append_field(m.msg, f.fld)) }
func (m *Msg) UpdateField(f *Field) error { return check(C.rai_msg_update_field(m.msg, f.fld)) }

/* explicitly typed append / update (Java names) */
func (m *Msg) AppendBool(n string, v bool) error      { return m.Append(n, v) }
func (m *Msg) AppendByte(n string, v int8) error      { return m.Append(n, v) }
func (m *Msg) AppendUByte(n string, v uint8) error    { return m.Append(n, v) }
func (m *Msg) AppendShort(n string, v int16) error    { return m.Append(n, v) }
func (m *Msg) AppendUShort(n string, v uint16) error  { return m.Append(n, v) }
func (m *Msg) AppendInt(n string, v int32) error      { return m.Append(n, v) }
func (m *Msg) AppendUInt(n string, v uint32) error    { return m.Append(n, v) }
func (m *Msg) AppendLong(n string, v int64) error     { return m.Append(n, v) }
func (m *Msg) AppendULong(n string, v uint64) error   { return m.Append(n, v) }
func (m *Msg) AppendFloat(n string, v float32) error  { return m.Append(n, v) }
func (m *Msg) AppendDouble(n string, v float64) error { return m.Append(n, v) }
func (m *Msg) AppendString(n string, v string) error  { return m.Append(n, v) }
func (m *Msg) UpdateBool(n string, v bool) error      { return m.Update(n, v) }
func (m *Msg) UpdateByte(n string, v int8) error      { return m.Update(n, v) }
func (m *Msg) UpdateUByte(n string, v uint8) error    { return m.Update(n, v) }
func (m *Msg) UpdateShort(n string, v int16) error    { return m.Update(n, v) }
func (m *Msg) UpdateUShort(n string, v uint16) error  { return m.Update(n, v) }
func (m *Msg) UpdateInt(n string, v int32) error      { return m.Update(n, v) }
func (m *Msg) UpdateUInt(n string, v uint32) error    { return m.Update(n, v) }
func (m *Msg) UpdateLong(n string, v int64) error     { return m.Update(n, v) }
func (m *Msg) UpdateULong(n string, v uint64) error   { return m.Update(n, v) }
func (m *Msg) UpdateFloat(n string, v float32) error  { return m.Update(n, v) }
func (m *Msg) UpdateDouble(n string, v float64) error { return m.Update(n, v) }
func (m *Msg) UpdateString(n string, v string) error  { return m.Update(n, v) }

/* ---- pack / unpack ------------------------------------------------------- */

/* UnPack parses a packed message; the api copies what it must own */
func (m *Msg) UnPack(buf []byte) error {
	return check(C.rai_msg_unpack(m.msg, bufPtr(buf), C.uint32_t(len(buf))))
}

/* PackInto packs the message into buf, returning the size */
func (m *Msg) PackInto(buf []byte) (int, error) {
	var size C.uint32_t
	err := check(C.rai_msg_pack(m.msg, bufPtr(buf), C.uint32_t(len(buf)), &size))
	return int(size), err
}

/* Pack returns a copy of the packed message */
func (m *Msg) Pack() ([]byte, error) {
	var p unsafe.Pointer
	var size C.uint32_t
	if err := check(C.rai_msg_packed(m.msg, &p, &size)); err != nil {
		return nil, err
	}
	return goBytes(p, size), nil
}
func (m *Msg) Packed() ([]byte, error) { return m.Pack() }
func (m *Msg) PackSize() (int, error) {
	var s C.uint32_t
	err := check(C.rai_msg_pack_size(m.msg, &s))
	return int(s), err
}

func (m *Msg) Activate(name string) (bool, error) {
	cn := cstr(name)
	defer cfree(cn)
	var ok C.int
	err := check(C.rai_msg_activate(m.msg, cn, &ok))
	return ok != 0, err
}
func (m *Msg) Rename(oldName, newName string) (bool, error) {
	co, cn := cstr(oldName), cstr(newName)
	defer cfree(co)
	defer cfree(cn)
	var ok C.int
	err := check(C.rai_msg_rename(m.msg, co, cn, &ok))
	return ok != 0, err
}
func (m *Msg) Remove(name string) (bool, error) {
	cn := cstr(name)
	defer cfree(cn)
	var ok C.int
	err := check(C.rai_msg_remove(m.msg, cn, &ok))
	return ok != 0, err
}

/* ---- printing ------------------------------------------------------------ */

/* Print the message to w, one field per line */
func (m *Msg) Print(w io.Writer) error { return m.PrintFmt(w, true, "", true, "", "") }

func (m *Msg) PrintFmt(w io.Writer, fieldNewlines bool, fnameFormat string, printOpaques bool,
	debugFormat, debugHFormat string) error {
	a := newWriter(w)
	defer a.release()
	cf, cd, ch := cstrOpt(fnameFormat), cstrOpt(debugFormat), cstrOpt(debugHFormat)
	defer cfree(cf)
	defer cfree(cd)
	defer cfree(ch)
	return check(C.rai_msg_print(m.msg, C.rai_write_fn(C.rai_go_write_fn), a.ptr(),
		b2i(fieldNewlines), cf, b2i(printOpaques), cd, ch))
}
func (m *Msg) PrintHex(w io.Writer) error {
	a := newWriter(w)
	defer a.release()
	return check(C.rai_msg_print_hex(m.msg, C.rai_write_fn(C.rai_go_write_fn), a.ptr()))
}

/* PrintHexBuf hex dumps a packed buffer */
func PrintHexBuf(w io.Writer, buf []byte) error {
	a := newWriter(w)
	defer a.release()
	return check(C.rai_msg_print_hex_buf(bufPtr(buf), C.uint32_t(len(buf)),
		C.rai_write_fn(C.rai_go_write_fn), a.ptr()))
}
func (m *Msg) PrintXML(w io.Writer, attrFlags int, printNewlines bool) error {
	a := newWriter(w)
	defer a.release()
	return check(C.rai_msg_print_xml(m.msg, C.rai_write_fn(C.rai_go_write_fn), a.ptr(),
		C.int(attrFlags), b2i(printNewlines)))
}
func (m *Msg) String() string {
	var sb strings.Builder
	if err := m.Print(&sb); err != nil {
		sb.WriteString("<" + err.Error() + ">")
	}
	return sb.String()
}

/* ---- Field ---------------------------------------------------------------- */

/* Field is a cursor over a message field: name, type, size, value, and
 * iteration (First / Next / Find).  The data it points at belongs to the
 * message and is valid only while the message is unchanged. */
type Field struct {
	fld C.rai_field_t
}

func NewField() (*Field, error) {
	f := C.rai_field_create()
	if f == nil {
		return nil, &Error{Module: "RaiField", Reason: "unable to create field"}
	}
	return &Field{fld: f}, nil
}
func (f *Field) Delete() {
	if f.fld != nil {
		C.rai_field_delete(f.fld)
		f.fld = nil
	}
}

func (f *Field) Name() string    { return goStr(C.rai_field_name(f.fld)) }
func (f *Field) Type() int       { return int(C.rai_field_type(f.fld)) }
func (f *Field) Size() int       { return int(C.rai_field_size(f.fld)) }
func (f *Field) HintType() int   { return int(C.rai_field_hint_type(f.fld)) }
func (f *Field) HintSize() int   { return int(C.rai_field_hint_size(f.fld)) }
func (f *Field) EntryType() int  { return int(C.rai_field_entry_type(f.fld)) }
func (f *Field) EntrySize() int  { return int(C.rai_field_entry_size(f.fld)) }
func (f *Field) NumEntries() int { return int(C.rai_field_num_entries(f.fld)) }
func (f *Field) Offset() int     { return int(C.rai_field_offset(f.fld)) }
func (f *Field) Fid() (uint16, bool) {
	var fid C.uint16_t
	ok := C.rai_field_fid(f.fld, &fid) != 0
	return uint16(fid), ok
}
func TypeToString(t int) string { return goStr(C.rai_field_type_string(C.int(t))) }

/* Get the value as the natural Go type (see Msg.Get) */
func (f *Field) Get() (any, error) {
	t, sz := f.Type(), f.Size()
	switch t {
	case RAIMSG_BOOLEAN:
		return f.GetBool()
	case RAIMSG_INT:
		switch sz {
		case 1:
			return f.GetByte()
		case 2:
			return f.GetShort()
		case 4:
			return f.GetInt()
		default:
			return f.GetLong()
		}
	case RAIMSG_UINT:
		switch sz {
		case 1:
			v, err := f.GetByte()
			return uint8(v), err
		case 2:
			v, err := f.GetShort()
			return uint16(v), err
		case 4:
			v, err := f.GetInt()
			return uint32(v), err
		default:
			v, err := f.GetLong()
			return uint64(v), err
		}
	case RAIMSG_REAL:
		if sz == 4 {
			return f.GetFloat()
		}
		return f.GetDouble()
	case RAIMSG_STRING:
		return f.GetString()
	case RAIMSG_OPAQUE, RAIMSG_IPDATA:
		return f.GetOpaque(), nil
	case RAIMSG_PARTIAL:
		return f.GetPartial(), nil
	case RAIMSG_MESSAGE:
		m, err := NewMsg(RAIMSG_PROTO)
		if err != nil {
			return nil, err
		}
		if err = f.GetMsg(m); err != nil {
			m.Delete()
			return nil, err
		}
		return m, nil
	case RAIMSG_ARRAY:
		switch f.EntryType() {
		case RAIMSG_BOOLEAN:
			return f.GetBoolArray()
		case RAIMSG_INT, RAIMSG_UINT:
			switch f.EntrySize() {
			case 1:
				return f.GetByteArray()
			case 2:
				return f.GetShortArray()
			case 4:
				return f.GetIntArray()
			default:
				return f.GetLongArray()
			}
		case RAIMSG_REAL:
			if f.EntrySize() == 4 {
				return f.GetFloatArray()
			}
			return f.GetDoubleArray()
		default:
			return f.GetStringArray()
		}
	}
	return nil, nil
}
func (f *Field) GetBool() (bool, error) {
	var v C.int
	err := check(C.rai_field_get_bool(f.fld, &v))
	return v != 0, err
}
func (f *Field) GetByte() (int8, error) {
	var v C.int8_t
	err := check(C.rai_field_get_i8(f.fld, &v))
	return int8(v), err
}
func (f *Field) GetShort() (int16, error) {
	var v C.int16_t
	err := check(C.rai_field_get_i16(f.fld, &v))
	return int16(v), err
}
func (f *Field) GetInt() (int32, error) {
	var v C.int32_t
	err := check(C.rai_field_get_i32(f.fld, &v))
	return int32(v), err
}
func (f *Field) GetLong() (int64, error) {
	var v C.int64_t
	err := check(C.rai_field_get_i64(f.fld, &v))
	return int64(v), err
}
func (f *Field) GetFloat() (float32, error) {
	var v C.float
	err := check(C.rai_field_get_f32(f.fld, &v))
	return float32(v), err
}
func (f *Field) GetDouble() (float64, error) {
	var v C.double
	err := check(C.rai_field_get_f64(f.fld, &v))
	return float64(v), err
}
func (f *Field) GetString() (string, error) {
	var s *C.char
	var n C.uint32_t
	err := check(C.rai_field_get_string(f.fld, &s, &n))
	return goStrN(s, n), err
}
func (f *Field) GetOpaque() []byte {
	return goBytes(C.rai_field_data(f.fld), C.rai_field_size(f.fld))
}
func (f *Field) GetPartial() Partial   { return Partial{Data: f.GetOpaque(), Offset: f.Offset()} }
func (f *Field) GetMsg(sub *Msg) error { return check(C.rai_field_get_msg(f.fld, sub.msg)) }

func (f *Field) entry(i int) (int64, error) {
	var v C.int64_t
	err := check(C.rai_field_get_entry_i64(f.fld, C.uint32_t(i), &v))
	return int64(v), err
}
func (f *Field) entryF(i int) (float64, error) {
	var v C.double
	err := check(C.rai_field_get_entry_f64(f.fld, C.uint32_t(i), &v))
	return float64(v), err
}
func (f *Field) GetBoolArray() ([]bool, error) {
	n := f.NumEntries()
	a := make([]bool, n)
	for i := 0; i < n; i++ {
		v, err := f.entry(i)
		if err != nil {
			return nil, err
		}
		a[i] = v != 0
	}
	return a, nil
}
func (f *Field) GetByteArray() ([]int8, error) {
	n := f.NumEntries()
	a := make([]int8, n)
	for i := 0; i < n; i++ {
		v, err := f.entry(i)
		if err != nil {
			return nil, err
		}
		a[i] = int8(v)
	}
	return a, nil
}
func (f *Field) GetShortArray() ([]int16, error) {
	n := f.NumEntries()
	a := make([]int16, n)
	for i := 0; i < n; i++ {
		v, err := f.entry(i)
		if err != nil {
			return nil, err
		}
		a[i] = int16(v)
	}
	return a, nil
}
func (f *Field) GetIntArray() ([]int32, error) {
	n := f.NumEntries()
	a := make([]int32, n)
	for i := 0; i < n; i++ {
		v, err := f.entry(i)
		if err != nil {
			return nil, err
		}
		a[i] = int32(v)
	}
	return a, nil
}
func (f *Field) GetLongArray() ([]int64, error) {
	n := f.NumEntries()
	a := make([]int64, n)
	for i := 0; i < n; i++ {
		v, err := f.entry(i)
		if err != nil {
			return nil, err
		}
		a[i] = v
	}
	return a, nil
}
func (f *Field) GetFloatArray() ([]float32, error) {
	n := f.NumEntries()
	a := make([]float32, n)
	for i := 0; i < n; i++ {
		v, err := f.entryF(i)
		if err != nil {
			return nil, err
		}
		a[i] = float32(v)
	}
	return a, nil
}
func (f *Field) GetDoubleArray() ([]float64, error) {
	n := f.NumEntries()
	a := make([]float64, n)
	for i := 0; i < n; i++ {
		v, err := f.entryF(i)
		if err != nil {
			return nil, err
		}
		a[i] = v
	}
	return a, nil
}
func (f *Field) GetStringArray() ([]string, error) {
	n := f.NumEntries()
	a := make([]string, n)
	for i := 0; i < n; i++ {
		var s *C.char
		var l C.uint32_t
		if err := check(C.rai_field_get_entry_string(f.fld, C.uint32_t(i), &s, &l)); err != nil {
			return nil, err
		}
		a[i] = goStrN(s, l)
	}
	return a, nil
}

/* iteration */
func (f *Field) Find(m *Msg, name string) (bool, error) {
	cn := cstr(name)
	defer cfree(cn)
	var found C.int
	err := check(C.rai_field_find(f.fld, m.msg, cn, &found))
	return found != 0, err
}
func (f *Field) First(m *Msg) (bool, error) {
	var more C.int
	err := check(C.rai_field_first(f.fld, m.msg, &more))
	return more != 0, err
}
func (f *Field) Next() (bool, error) {
	var more C.int
	err := check(C.rai_field_next(f.fld, &more))
	return more != 0, err
}
