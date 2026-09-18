/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 *
 * C trampolines: the api calls these function pointers, they forward to the
 * exported Go functions in callbacks.go.  A cgo file that exports Go
 * functions may only declare C functions in its preamble, hence this file. */
#include <stdint.h>
#include <raiapi2_c.h>
#include "_cgo_export.h"

/* a runtime/cgo.Handle (uintptr) as the void * closure the api stores; done
 * in C so go vet's unsafeptr check has nothing to complain about */
void *
rai_go_handle_ptr( uintptr_t h )
{
  return (void *) h;
}

void
rai_go_msg_fn( void *cl,  const rai_msg_event_t *ev,  rai_msg_t msg )
{
  goMsgFn( cl, (rai_msg_event_t *) ev, msg );
}

void
rai_go_timer_fn( void *cl,  rai_timer_t timer )
{
  goTimerFn( cl, timer );
}

void
rai_go_subscribe_fn( void *cl,  const rai_subscribe_event_t *ev,  rai_msg_t msg )
{
  goSubscribeFn( cl, (rai_subscribe_event_t *) ev, msg );
}

void
rai_go_dataloss_fn( void *cl,  const rai_dataloss_event_t *ev )
{
  goDataLossFn( cl, (rai_dataloss_event_t *) ev );
}

void
rai_go_connection_fn( void *cl,  const rai_connection_event_t *ev )
{
  goConnectionFn( cl, (rai_connection_event_t *) ev );
}

uint32_t
rai_go_write_fn( void *cl,  const uint8_t *buf,  uint32_t len )
{
  return goWriteFn( cl, (uint8_t *) buf, len );
}
