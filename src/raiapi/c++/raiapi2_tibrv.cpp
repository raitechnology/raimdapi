/* Copyright (c) 2009 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_api__raiapi2_tibrv_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#include <sassrv/rv7api.h>
#include "raiapi2.h"
#include "base/log.h"
#include "base/rai_factory.h"
#include "base/thread.h"
#include "util/args.h"
#include "msg/sass_const.h"
#include "msg/mfeed_dict.h"
#include "msg/subject.h"
#include "stream/byte_array_stream.h"
#include "util/str_util.h"

using namespace rai;

extern "C" RAI_DLL_EXPORT RaiApi *RaiApi_RaiOpen_tibrv( int argc,  char *argv[] );

struct RaiQueue_tibrv;
struct RaiSubscribe_tibrv;

struct RaiApi_tibrv : public RaiApi {
  char * daemon,
       * service,
       * network,
       * snapPrefix;

  SYS_OPS( RaiApi_tibrv );
  RaiApi_tibrv() : daemon( 0 ), service( 0 ), network( 0 ), snapPrefix( 0 ) {}

  virtual ~RaiApi_tibrv();

  virtual const char *GetApiName( void ) { return "tibrv"; }

  virtual void GetArgs( Args &args );
    
  virtual void ParseArgs( Args &args );

  virtual RaiSession *CreateSession( void );

  virtual void Close( void );

  virtual bool SetIoctl( const char *parameter,  const void *value )
;
  virtual bool GetIoctl( const char *parameter,  void *value );

  static RaiException getRvErr( tibrv_status status );
};


struct RaiSession_tibrv : public RaiSession, public Thread {
  RaiApi_tibrv        & api;
  tibrvTransport        rvT;
  tibrvQueue            xQ;
  tibrvId               warn, err;
  Mutex               * lock;
  RaiQueue_tibrv      * qHd, * qTl;
  char                * transportName;
  RaiDataLossCallback * dataLossCb;
  void                * dataLossCl;
  bool                  quit;

  SYS_OPS( RaiSession_tibrv );
  RaiSession_tibrv( RaiApi_tibrv *a ) : Thread( "tibrvqueue" ),
    api( *a ), rvT( 0 ), xQ( 0 ), warn( TIBRV_INVALID_ID ),
    err( TIBRV_INVALID_ID ), lock( 0 ), qHd( 0 ), qTl( 0 ),
    transportName( 0 ), dataLossCb( 0 ), dataLossCl( 0 ),
    quit( false ) {}
  virtual ~RaiSession_tibrv();

  /* RaiSession */
  virtual void Start( void );

  virtual RaiQueue *CreateQueue( bool direct );

  virtual RaiPublish *CreatePublish( bool autoInc );

  virtual RaiDict *CreateDict( void );

  virtual void Destroy( void );

  virtual RaiEntitlement *Login( const char *user );

  virtual void SetDataLossCB( RaiDataLossCallback *cb,  void *closure )
;
  virtual void NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus )
;
  virtual RaiApi *GetApi( void ) { return &this->api; }

  /* Thread */
  virtual void run( void );

  static void rvSys_onMsg( tibrvEvent I,  tibrvMsg msg,  void *cl );

  static void quit_onTimer( tibrvId I,  tibrvMsg msg, void *cl );

  void addQueueList( RaiQueue_tibrv *q );

  void rmQueueList( RaiQueue_tibrv *q );
};


struct RaiQueue_tibrv : public RaiQueue {
  RaiSession_tibrv   & session;
  tibrvQueue           rvQ;
  RaiQueue_tibrv     * next, * back;
  RaiSubscribe_tibrv * subHd, * subTl;
  Mutex              * apiLock;
  unsigned int         matchClock,
                       subClock;
  bool                 inList,
                       isDestroyed;

  SYS_OPS( RaiQueue_tibrv );
  RaiQueue_tibrv( RaiSession_tibrv &s,  tibrvQueue q )
    : session( s ), rvQ( q ), next( 0 ), back( 0 ), subHd( 0 ), subTl( 0 ),
      apiLock( 0 ), matchClock( 0 ), subClock( 0 ), inList( false ),
      isDestroyed( false ) {}
  virtual ~RaiQueue_tibrv();

  /* RaiQueue */
  virtual RaiTimer *CreateTimer( RaiTimerCallback *cb,  void *closure )
;
  virtual RaiSubscribe *CreateSubscribe( RaiMsgCallback *cb,  void *closure )
;
  virtual RaiInteractivePublish *CreateInteractivePublish(
                                      RaiSubscribeCallback *cb,
                                      void *closure );
  virtual void NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus )
;
  struct AppEvent {
    tibrvEvent       rvE;
    RaiAppCallback * cb;
    void           * eventData;
    Rai_i32          eventEnum;

    SYS_OPS( AppEvent );
    AppEvent( RaiAppCallback *c,  void *ed,  Rai_i32 en )
      : rvE( TIBRV_INVALID_ID ), cb( c ), eventData( ed ), eventEnum( en ) {}
  };
  virtual void QueueEvent( RaiAppCallback *cb,  void *eventData,
                           Rai_i32 eventEnum,  Rai_u8 eventPriority,
                           Rai_u32 expireMSecs );
  RaiException sendStatusMsg( RaiMsg &msg, RaiSubscribe::RaiSubState recvState,
                              Rai_u16 msgType,  Rai_u16 recStatus );
  virtual void Mainloop( void );

  virtual void TimedDispatch( Rai_u32 ivalMSecs );

  virtual void Dispatch( void );

  virtual Rai_u32 GetDepth( void );

  virtual RaiSession *GetSession( void ) { return &this->session; }

  virtual void Destroy( void );

  void addSubList( RaiSubscribe_tibrv *s );

  void rmSubList( RaiSubscribe_tibrv *s );

  static void onAppTimer( tibrvEvent id,  tibrvMsg msg,  void *cl );
};


struct RaiDict_tibrv : public RaiDict {
  RaiSession_tibrv & session;
  tibrvId            rvD;
  TimeMSecs          startTime;
  Rai_u32            timeoutSecs;
  bool               complete;

  SYS_OPS( RaiDict_tibrv );
  RaiDict_tibrv( RaiSession_tibrv &s ) : session( s ), rvD( TIBRV_INVALID_ID ),
    startTime( 0 ), timeoutSecs( 0 ), complete( false ) {}
  virtual ~RaiDict_tibrv();

  /* RaiDict */
  virtual void Load( Rai_u32 timeoutSecs,  const char *dictSubject,
                     bool loadWait );
  virtual bool HaveDict( void ) {
    return this->complete;
  }
  virtual bool InProgress( void );

  virtual RaiSession *GetSession( void ) { return &this->session; }

  static void onMsg( tibrvEvent I,  tibrvMsg msg,  void *cl );
};


struct RaiTimer_tibrv : public RaiTimer {
  RaiQueue_tibrv   & q;
  RaiTimerCallback & cb;
  TimeMSecs          ival;
  void             * cl;
  tibrvEvent         rvE;

  SYS_OPS( RaiTimer_tibrv );
  RaiTimer_tibrv( RaiQueue_tibrv &qu,  RaiTimerCallback *c,  void *closure )
    : q( qu ), cb( *c ), ival( 0 ), cl( closure ),
      rvE( TIBRV_INVALID_ID ) {}
  virtual ~RaiTimer_tibrv();

  /* RaiTimer */
  virtual void Start( void );

  virtual void Stop( void );

  virtual TimeMSecs  GetInterval( void );

  virtual void SetInterval( TimeMSecs  interval );

  virtual RaiQueue *GetQueue( void ) { return &this->q; }

  static void onTimer( tibrvEvent id,  tibrvMsg msg,  void *cl );
};


struct RaiSubscribe_tibrv : public RaiSubscribe {
  RaiQueue_tibrv     & q;
  RaiMsgCallback     & cb;
  void               * cl;
  char               * subject;
  const char         * reply;
  RaiSubscribe_tibrv * next, * back;
  tibrvEvent           snapId, listenId;
  RaiMsg             * extra;
  unsigned int         match,
                       count;
  byte                 subType;
  bool                 inList,
                       hasTimer,
                       usePrefix;

  SYS_OPS( RaiSubscribe_tibrv );
  RaiSubscribe_tibrv( RaiQueue_tibrv &qu,  RaiMsgCallback *c,  void *closure )
    : q( qu ), cb( *c ), cl( closure ), subject( 0 ), reply( 0 ),
      next( 0 ), back( 0 ),
      snapId( TIBRV_INVALID_ID ), listenId( TIBRV_INVALID_ID ), extra( 0 ),
      match( 0 ), count( 0 ), subType( 0xff ), inList( false ),
      hasTimer( false ), usePrefix( true ){}
  virtual ~RaiSubscribe_tibrv();

  virtual void Start( const char *subject,  RaiSubParameter parm,
                      Rai_u32 timeoutMSecs );
  void sendSubRequest( void );

  virtual void Cancel( void );
  
  virtual void Refresh( Rai_u32 timeoutMSecs );

  virtual const char *Subject( void );
  
  virtual bool InProgress( void );

  virtual RaiQueue *GetQueue( void ) { return &this->q; }
  /* Set extra parameters passed with subscription at Start(), null erases */
  virtual void SetExtra( RaiMsg *ex );

  static void onMsg( tibrvEvent id,  tibrvMsg msg,  void *cl );
};


struct RaiSubscribeTimer_tibrv {
  RaiSession_tibrv   & session;
  RaiQueue_tibrv     & q;
  RaiSubscribe_tibrv & sub;
  unsigned int         count;

  SYS_OPS( RaiSubscribeTimer_tibrv );
  RaiSubscribeTimer_tibrv( RaiSubscribe_tibrv & s ) : session( s.q.session ),
     q( s.q ), sub( s ), count( s.count ) {}

  static void onTimer( tibrvEvent id,  tibrvMsg msg,  void *cl );
};


struct RaiPublish_tibrv : public RaiPublish {
  RaiSession_tibrv & session;

  SYS_OPS( RaiPublish_tibrv );
  RaiPublish_tibrv( RaiSession_tibrv &s,  bool autoInc )
    : RaiPublish( autoInc ), session( s ) {}
  virtual ~RaiPublish_tibrv();

  virtual void Publish( const char *subject,  const void *buffer,  
                        Rai_u32 size,  TimeNSecs stamp,
                        Rai_u32 msgTypeId );
  virtual void Destroy( void );

  virtual RaiSession *GetSession( void ) { return &this->session; }
};


struct RaiInteractivePublish_tibrv : public RaiInteractivePublish,
                                     public RaiPublish_tibrv,
                                     public RaiMsgCallback {
  RaiSession_tibrv     & session;
  RaiQueue_tibrv       & q;
  RaiSubscribeCallback & cb;
  void                 * cl;
  char                 * subject;
  RaiSubscribe_tibrv   * inSub,
                       * inCxl,
                       * inSnp;

  SYS_OPS( RaiInteractivePublish_tibrv );
  RaiInteractivePublish_tibrv( RaiQueue_tibrv &qu,  RaiSubscribeCallback *c,
                               void *closure ) 
   : RaiPublish_tibrv( qu.session, false ), session( qu.session ), q( qu ),
    cb( *c ), cl( closure ), subject( 0 ), inSub( 0 ), inCxl( 0 ), inSnp( 0 ) {}
  virtual ~RaiInteractivePublish_tibrv();

  virtual void Publish( const char *subject,  const void *buffer,  
                        Rai_u32 size,  TimeNSecs stamp,
                        Rai_u32 msgTypeId );
  virtual void Destroy( void );

  virtual RaiSession *GetSession( void ) { return &this->session; }

  virtual void InteractiveStart( const char *subject );

  virtual void InteractiveCancel( void );

  virtual bool InProgress( void );

  virtual RaiQueue *GetQueue( void ) { return &this->q; }

  void onMsg( RaiMsgEvent &event,  RaiMsg &msg,  void *cl );
};


RAI_DLL_EXPORT RaiApi *
RaiApi_RaiOpen_tibrv( int /* argc */,  char * /* argv */[] )
{
  tibrv_status status = tibrv_Open();
  if ( status != TIBRV_OK ) {
    Error e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "tibrv_Open() failed" );
  }
  return NEW RaiApi_tibrv();
}


RaiApi_tibrv::~RaiApi_tibrv()
{
  if ( this->daemon != NULL )
    FREE( this->daemon );
  if ( this->service != NULL )
    FREE( this->service );
  if ( this->network != NULL )
    FREE( this->network );
  if ( this->snapPrefix != NULL )
    FREE( this->snapPrefix );
}


static const char network_arg[]     = "network",
                  service_arg[]     = "service",
                  daemon_arg[]      = "daemon",
                  snap_prefix_arg[] = "snapPrefix";
static const char * prefix_null_check( const char *value ) {
  if ( value != NULL &&
       ( value[ 0 ] == '\0' || ::strcmp( value, "\"\"" ) == 0  ||
         ::strcmp( value, "-" ) == 0 ) )
    value = NULL;
  return value;
}

void
RaiApi_tibrv::GetArgs( Args &args )
{
  static StringArg network( network_arg, NULL, "<network>",
                            "RV network of interface to use" );
  static StringArg service( service_arg, NULL, "<num>",
                            "RV service to use" );
  static StringArg daemon(  daemon_arg, NULL, "<address>",
                            "RV daemon to connect to" );
  static StringArg snapPref( snap_prefix_arg, "_SNAP.", "<string>",
                            "Prefix to use for snapshot and initial values" );

  this->RaiApi::GetArgs( args );
  args.add( &network, COMMAND_ARG | RESOURCE_ARG );
  args.add( &service, COMMAND_ARG | RESOURCE_ARG );
  args.add( &daemon, COMMAND_ARG | RESOURCE_ARG );
  args.add( &snapPref, COMMAND_ARG | RESOURCE_ARG );
}

    
void
RaiApi_tibrv::ParseArgs( Args &args )
{
  const char *s;

  this->RaiApi::ParseArgs( args );
  s = args.getString( network_arg );
  if ( s != NULL )
    STRDUP( this->network, s );
  s = args.getString( service_arg );
  if ( s != NULL )
    STRDUP( this->service, s );
  s = args.getString( daemon_arg );
  if ( s != NULL )
    STRDUP( this->daemon, s );
  s = prefix_null_check( args.getString( snap_prefix_arg ) );
  if ( s != NULL )
    STRDUP( this->snapPrefix, s );
}


void
RaiApi_tibrv::Close( void )
{
  tibrv_Close();
}


bool
RaiApi_tibrv::SetIoctl( const char *parameter,
                        const void *value )
{
  if ( ::strcmp( parameter, daemon_arg ) == 0 )
    STRDUP( this->daemon, (const char *) value );
  else if ( ::strcmp( parameter, service_arg ) == 0 )
    STRDUP( this->service, (const char *) value );
  else if ( ::strcmp( parameter, network_arg ) == 0 )
    STRDUP( this->network, (const char *) value );
  else if ( ::strcmp( parameter, snap_prefix_arg ) == 0 ) {
    STRDUP( this->snapPrefix, prefix_null_check( (const char *) value ) );
  }
  else
    return this->RaiApi::SetIoctl( parameter, value );
  return true;
}


bool
RaiApi_tibrv::GetIoctl( const char *parameter,
                        void *value )
{
  if ( ::strcmp( parameter, daemon_arg ) == 0 )
    *(char **) value = this->daemon;
  else if ( ::strcmp( parameter, service_arg ) == 0 )
    *(char **) value = this->service;
  else if ( ::strcmp( parameter, network_arg ) == 0 )
    *(char **) value = this->network;
  else if ( ::strcmp( parameter, snap_prefix_arg ) == 0 )
    *(char **) value = this->snapPrefix;
  else
    return this->RaiApi::GetIoctl( parameter, value );
  return true;
}


void
RaiSession_tibrv::rvSys_onMsg( tibrvEvent /* I */,  tibrvMsg msg,  void *cl )
{
  static const char /*SLOWCONSUMER[]   = "CLIENT.SLOWCONSUMER",
                    ERROR_EXPIRE[]   = "ERROR.SYSTEM.LICENSE.EXPIRE",
                    WARN_EXPIRE[]    = "WARN.SYSTEM.LICENSE.EXPIRE",*/
                    OUTBOUND_PTP[]   = "DATALOSS.OUTBOUND.PTP",
                    OUTBOUND_BCAST[] = "DATALOSS.OUTBOUND.BCAST",
                    INBOUND_PTP[]    = "DATALOSS.INBOUND.PTP",
                    INBOUND_BCAST[]  = "DATALOSS.INBOUND.BCAST",
                    LOST[]           = "lost";
  RaiSession_tibrv * session = (RaiSession_tibrv *) cl;
  RaiDataLossEvent   event( *session );
  RaiMsg             raiMsg;
  const void       * bufp;
  const char       * subj;
  tibrv_u32          msgSize;
  tibrv_status       status;
  unsigned int       idl = 0, odl = 0;
  char               buf[ 4 * 1024 ];
  ByteArrayOutputStream bout( (byte *) buf, sizeof( buf ) - 1 );

  if ( (status = tibrvMsg_GetByteSize( msg, &msgSize )) == TIBRV_OK )
    status = tibrvMsg_GetAsBytes( msg, &bufp );
  status = tibrvMsg_GetSendSubject( msg, &subj );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Getting _RV.*.SYSTEM send subject" );
    return;
  }
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Getting %s message bytes", subj );
    return;
  }
  try {
    raiMsg.UnPack( RV_PROTO, (void *) bufp, msgSize, RAIMSG_MEMORY_STATIC );
    bout.printf( "%s = {", subj );
    raiMsg.Print( &bout, 0, "%s=", 0, NULL, NULL );
    bout.printf( "} (%s)", session->transportName );
    buf[ bout.length() ] = '\0';

    if ( session->dataLossCb != NULL ) {
      if ( ::strstr( subj, OUTBOUND_PTP ) != NULL ||
           ::strstr( subj, OUTBOUND_BCAST ) != NULL )
       raiMsg.Get( LOST, odl );
      else if ( ::strstr( subj, INBOUND_PTP ) != NULL ||
                ::strstr( subj, INBOUND_BCAST ) != NULL )
       raiMsg.Get( LOST, idl );

      if ( idl != 0 || odl != 0 ) {
        event.transportName      = session->transportName;
        event.description        = buf;
        event.inboundPacketLoss  = idl;
        event.outboundPacketLoss = odl;
        event.connectionLoss     = false;
        event.isMulticast        = true;
        event.connectionCount    = 1;

        session->dataLossCb->onDataLoss( event, session->dataLossCl );
      }
      else {
        RaiException e = RaiApiErr::getErr( RaiApiErr::TSPT_DATALOSS );
        logError( LERROR, e, "%s", buf );
      }
    }
    else {
      logError( LERROR, NULL, "%s", buf );
    }
  } catch ( Error e ) {
    logError( LERROR, e, "Unpack %s failed", subj );
    return;
  }
}


RaiSession *
RaiApi_tibrv::CreateSession( void )
{
  RaiSession_tibrv * session;
  tibrv_status       status;
  char               buf[ 4 * 1024 ];
  ByteArrayOutputStream bout( (byte *) buf, sizeof( buf ) - 1 );

  session = NEW RaiSession_tibrv( this );

  session->lock = Mutex::create();
  status = tibrvQueue_Create( &session->xQ );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  if ( this->daemon != NULL )
    bout.printf( "dmn=%s%s", this->daemon,
                 ( this->network || this->service ) ? ", " : "" );
  if ( this->network != NULL )
    bout.printf( "net=%s%s", this->network, ( this->service ) ? ", " : "" );
  if ( this->service != NULL )
    bout.printf( "svc=%s", this->service );
  if ( bout.length() == 0 )
    bout.puts( "(default)" );
  buf[ bout.length() ] = '\0';
  STRDUP( session->transportName, buf );

  return session;
}

void
RaiSession_tibrv::Start( void )
{
  static const char RV_ERROR_SUBJECT[] = "_RV.ERROR.SYSTEM.>",
                    RV_WARN_SUBJECT[]  = "_RV.WARN.SYSTEM.>";
  tibrv_status status;

  status = tibrvTransport_Create( &this->rvT, this->api.service,
                                  this->api.network, this->api.daemon );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchMode( this->rvT, TIBRV_TRANSPORT_SINGLE_BATCH );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchSize( this->rvT, 16384 );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchInterval( this->rvT, 0.1 );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchDispatchFlush( this->rvT, TIBRV_TRUE );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Error in tibrvTransport_Create()" );
    throw e;
  }

  status = tibrvEvent_CreateListener( &this->warn, this->xQ,
                                      RaiSession_tibrv::rvSys_onMsg,
                                      this->rvT, RV_WARN_SUBJECT, this );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  status = tibrvEvent_CreateListener( &this->err, this->xQ,
                                      RaiSession_tibrv::rvSys_onMsg,
                                      this->rvT, RV_ERROR_SUBJECT, this );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  if ( this->dataLossCb != NULL ) {
    RaiConnectionEvent event( *this );
    char descrBuf[ 2048 ];
    ByteArrayOutputStream bout( (byte *) descrBuf, sizeof( descrBuf ) - 1 );

    bout.printf( "Connected to daemon '%s'", this->api.daemon != NULL ?
                 this->api.daemon : "tcp:7500" );
    descrBuf[ bout.length() ] = '\0';
    event.transportName      = this->transportName;
    event.description        = descrBuf;
    event.connectionOriented = false;
    event.isMulticast        = true;
    event.connectionCount    = 1;

    this->dataLossCb->onConnection( event, this->dataLossCl );
  }
  this->Thread::start();
}

void
RaiQueue_tibrv::addSubList( RaiSubscribe_tibrv *s )
{
  if ( ! s->inList ) {
    s->inList = true;
    if ( (s->back = this->subTl) != NULL )
      this->subTl->next = s;
    else
      this->subHd = s;
    this->subTl = s;
  }
}


void
RaiQueue_tibrv::rmSubList( RaiSubscribe_tibrv *s )
{
  if ( s->inList ) {
    s->inList = true;
    if ( s->next != NULL )
      s->next->back = s->back;
    else
      this->subTl = s->back;
    if ( s->back != NULL )
      s->back->next = s->next;
    else
      this->subHd = s->next;
    s->next = s->back = NULL;
  }
}


void
RaiSession_tibrv::addQueueList( RaiQueue_tibrv *q )
{
  this->lock->lock();
  if ( ! q->inList ) {
    q->inList = true;
    if ( (q->back = this->qTl) != NULL )
      this->qTl->next = q;
    else
      this->qHd = q;
    this->qTl = q;
  }
  this->lock->unlock();
}


void
RaiSession_tibrv::rmQueueList( RaiQueue_tibrv *q )
{
  this->lock->lock();
  if ( q->inList ) {
    q->inList = false;
    if ( q->next != NULL )
      q->next->back = q->back;
    else
      this->qTl = q->back;
    if ( q->back != NULL )
      q->back->next = q->next;
    else
      this->qHd = q->next;
    q->next = q->back = NULL;
  }
  this->lock->unlock();
}


void
RaiSession_tibrv::quit_onTimer( tibrvId I,  tibrvMsg /* msg */, void *cl )
{
  RaiSession_tibrv *me = (RaiSession_tibrv *) cl;
  tibrvEvent_Destroy( (tibrvEvent) I );
  me->quit = true;
}


void
RaiSession_tibrv::run( void ) 
{
  tibrv_status status;

  while ( ! this->quit ) {
    status = tibrvQueue_Dispatch( this->xQ );
    if ( status != TIBRV_OK ) {
      RaiException e = RaiApi_tibrv::getRvErr( status );
      logError( LERROR, e, "RaiSession dispatch thread" );
      break;
    }
  }
  this->exit();
}


RaiSession_tibrv::~RaiSession_tibrv()
{
  if ( this->transportName != NULL )
    FREE( this->transportName );
  if ( this->lock != NULL ) 
    delete this->lock;
}


RaiQueue *
RaiSession_tibrv::CreateQueue( bool /* direct */ )
{
  RaiQueue_tibrv * q;
  tibrvQueue       rvQ;
  tibrv_status     status;

  status = tibrvQueue_Create( &rvQ );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  q = NEW RaiQueue_tibrv( *this, rvQ );
  q->apiLock = Mutex::create( Mutex::RECURSIVE_LOCK );
  this->addQueueList( q );
  return q;
}


RaiPublish *
RaiSession_tibrv::CreatePublish( bool autoInc )
{
  return NEW RaiPublish_tibrv( *this, autoInc );
}


RaiDict *
RaiSession_tibrv::CreateDict( void )
{
  return NEW RaiDict_tibrv( *this );
}


RaiTimer *
RaiQueue_tibrv::CreateTimer( RaiTimerCallback *cb,
                             void *closure )
{
  return NEW RaiTimer_tibrv( *this, cb, closure );
}


RaiSubscribe *
RaiQueue_tibrv::CreateSubscribe( RaiMsgCallback *cb,
                                 void *closure )
{
  return NEW RaiSubscribe_tibrv( *this, cb, closure );
}


RaiInteractivePublish *
RaiQueue_tibrv::CreateInteractivePublish( RaiSubscribeCallback *cb,
                                          void *closure )
{
  return NEW RaiInteractivePublish_tibrv( *this, cb, closure );
}


void 
RaiSession_tibrv::Destroy( void )
{
  if ( ! this->quit && this->isThreadRunning() ) {
    tibrvId timerId;
    tibrvEvent_CreateTimer( &timerId, this->xQ, RaiSession_tibrv::quit_onTimer,
                            0.0, this );
  }
  if ( ! this->isThreadJoined() )
    this->join();
  if ( this->warn != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->warn );
    this->warn = TIBRV_INVALID_ID;
  }
  if ( this->err != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->err );
    this->err = TIBRV_INVALID_ID;
  }
  if ( this->xQ != 0 ) {
    tibrvQueue_Destroy( this->xQ );
    this->xQ = 0;
  }
  if ( this->rvT != 0 ) {
    tibrvTransport_Destroy( this->rvT );
    this->rvT = 0;
  }
}


void 
RaiQueue_tibrv::Destroy( void )
{
  this->apiLock->lock();
  this->isDestroyed = true;
  this->apiLock->unlock();
  if ( this->rvQ != 0 ) {
    tibrvQueue_Destroy( this->rvQ );
    this->rvQ = 0;
  }
  this->session.rmQueueList( this );
}


RaiQueue_tibrv::~RaiQueue_tibrv()
{
  if ( this->rvQ != 0 )
    this->Destroy();
  if ( this->apiLock != NULL )
    delete this->apiLock;
}


void 
RaiQueue_tibrv::Mainloop( void )
{
  tibrv_status status;

  for (;;) {
    status = tibrvQueue_Dispatch( this->rvQ );
    if ( this->isDestroyed )
      return;
    if ( status != TIBRV_OK )
      throw RaiApi_tibrv::getRvErr( status );
  }
}


void 
RaiQueue_tibrv::TimedDispatch( Rai_u32 ivalMSecs )
{
  tibrv_status status;

  status = tibrvQueue_TimedDispatch( this->rvQ, (tibrv_f64) ivalMSecs / 1000.0);
  if ( this->isDestroyed )
    return;
  if ( status != TIBRV_OK && status != TIBRV_TIMEOUT )
    throw RaiApi_tibrv::getRvErr( status );
}


void 
RaiQueue_tibrv::Dispatch( void )
{
  tibrv_status status;

  status = tibrvQueue_Dispatch( this->rvQ );
  if ( this->isDestroyed )
    return;
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
}


Rai_u32
RaiQueue_tibrv::GetDepth( void )
{
  tibrv_u32 count;
  tibrv_status status;

  status = tibrvQueue_GetCount( this->rvQ, &count );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
  return count;
}


RaiEntitlement *
RaiSession_tibrv::Login( const char * /* user */ )
{
  return NULL;
}


void
RaiSession_tibrv::SetDataLossCB( RaiDataLossCallback *cb,
                                 void *closure )
{
  this->dataLossCl = closure;
  this->dataLossCb = cb;
}


static inline void
create_status_msg( RaiMsg &msg,  RaiSubscribe::RaiSubState &recvState,
                   Rai_u16 msgType,  Rai_u16 recStatus )
{
  recvState = RaiSubscribe::SassToSubState( msgType, recStatus );

  if ( DataDictionary != NULL &&
       DataDictionary->msgType != NULL && 
       DataDictionary->recStatus != NULL ) {
    msg.SetProtocol( TIB_SASS_PROTO );
    msg.Append( DataDictionary->msgType, msgType );
    msg.Append( DataDictionary->recStatus, recStatus );
  }
  else {
    msg.Append( "MSG_TYPE", msgType );
    msg.Append( "REC_STATUS", recStatus );
  }
}


void
RaiQueue_tibrv::NotifyStatus( Rai_u16 msgType,
                              Rai_u16 recStatus )
{
  RaiMsg msg;
  Error  e2;
  RaiSubscribe::RaiSubState recvState;

  create_status_msg( msg, recvState, msgType, recStatus );

  e2 = this->sendStatusMsg( msg, recvState, msgType, recStatus );

  if ( e2 != NULL )
    throw e2;
}


void
RaiQueue_tibrv::QueueEvent( RaiAppCallback *cb,  void *eventData,
                            Rai_i32 eventEnum,  Rai_u8 /* eventPriority */,
                            Rai_u32 expireMSecs )
{
  tibrv_f64    rv_interval;
  tibrv_status status;
  AppEvent   * p = NEW AppEvent( cb, eventData, eventEnum );

  if ( expireMSecs != 0 )
    rv_interval = (double) expireMSecs / 1000.0;
  else
    rv_interval = 0;
  status = tibrvEvent_CreateTimer( &p->rvE, this->rvQ,
                                   RaiQueue_tibrv::onAppTimer, rv_interval, p );
  if ( status != TIBRV_OK ) {
    delete p;
    throw RaiApi_tibrv::getRvErr( status );
  }
}


void
RaiQueue_tibrv::onAppTimer( tibrvEvent /* id */,  tibrvMsg /* msg */,  void *cl )
{
  AppEvent * p = (AppEvent *) cl;
  try {
    p->cb->onAppEvent( p->eventData, p->eventEnum );
  } catch ( Error e ) {
    logError( LERROR, e, "onAppEvent()" );
  }
  tibrvEvent_Destroy( p->rvE );
  delete p;
}


void
RaiSession_tibrv::NotifyStatus( Rai_u16 msgType,
                                Rai_u16 recStatus )
{
  RaiMsg msg;
  Error  e, e2;
  RaiSubscribe::RaiSubState recvState;
  RaiQueue_tibrv * q;

  create_status_msg( msg, recvState, msgType, recStatus );
  e2 = NULL;

  this->lock->lock();
  for ( q = this->qHd; q != NULL; q = q->next ) {
    e = q->sendStatusMsg( msg, recvState, msgType, recStatus );
    if ( e != NULL )
      e2 = e;
  }
  this->lock->unlock();

  if ( e2 != NULL )
    throw e2;
}


RaiException
RaiQueue_tibrv::sendStatusMsg( RaiMsg &msg, RaiSubscribe::RaiSubState recvState,
                               Rai_u16 msgType,  Rai_u16 recStatus )
{
  RaiSubscribe_tibrv * sub;
  unsigned int         subClock;
  RaiException         e2 = NULL;

  this->apiLock->lock();
  if ( ++this->matchClock == 0 ) {
    this->matchClock = 1;
    for ( sub = this->subHd; sub != NULL; sub = sub->next )
      sub->match = 0;
  }
  try {
    subClock = this->subClock; /* if list loses or gains a subject */
  rescan_list:;
    for ( sub = this->subHd; sub != NULL; sub = sub->next ) {
      if ( sub->match != this->matchClock ) {
        sub->match = this->matchClock;
        RaiSubscribe::RaiSubState oldState = sub->state;
        sub->state = RaiSubscribe::NewState( oldState, recvState );
        RaiMsgEvent ev( *sub, sub->subject, RaiMsgEvent::UPDATE, msgType,
                       recStatus, oldState, recvState, sub->state, 0, 0, 0, 0 );
        sub->cb.onMsg( ev, msg, sub->cl );
        if ( this->subClock != subClock ) {
          subClock = this->subClock;
          goto rescan_list;
        }
      }
    }
  } catch ( RaiException e ) {
    logError( LERROR, e, "Notify %s", sub->subject );
    e2 = e;
  }
  this->apiLock->unlock();
  return e2;
}


RaiDict_tibrv::~RaiDict_tibrv()
{
  if ( this->rvD != TIBRV_INVALID_ID )
    tibrvEvent_Destroy( this->rvD );
}


void
RaiDict_tibrv::onMsg( tibrvEvent /* id */,  tibrvMsg msg,  void *cl )
{
  RaiDict_tibrv & me = *(RaiDict_tibrv *) cl;
  RaiMsg          raiMsg;
  const void    * bufp;
  RaiMsg_config * dict,
                * dict2;
  RaiMfeed_dict * mdict,
                * mdict2;
  tibrv_u32       msgSize;
  tibrv_status    status;

  // see if we can unpack the dictionary
  dict2  = RaiMsg::GetDataDictionary();
  mdict2 = RaiMfeed_dict::GetMfeedDictionary();
  status = tibrvMsg_GetOpaque( msg, "_data_", &bufp, &msgSize );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Expecting opaque _data_ field" );
    return;
  }

  try {
    raiMsg.UnPack( (char * ) bufp, msgSize );
    dict = RaiMsg_config::unpackDataDictionary( raiMsg );
    RaiMsg::SetDataDictionary( dict );
    if ( (mdict = RaiMfeed_dict::unpackDataDictionary2( raiMsg )) != NULL ) {
      RaiMfeed_dict::SetMfeedDictionary( mdict );
      mdict->indexSass();
    }
    else {
      RaiMfeed_dict::SetMfeedDictionary( NULL );
    }
  } catch ( Error e ) {
    logError( LERROR, e, "Unpack data dictionary failed" );
    return; // cope with rv bug where license warning comes on inbox
  }
  logDebug( LDEBUG, "dictionary received" );

  if ( dict2 != NULL )
    RaiMsg_config::release( dict2 );
  if ( mdict2 != NULL )
    RaiMfeed_dict::release( mdict2 );

  tibrvEvent_Destroy( me.rvD );
  me.rvD = TIBRV_INVALID_ID;
  me.complete = true;
}


void
RaiDict_tibrv::Load( Rai_u32 timeoutSecs,  const char *dictSubject,
                     bool loadWait )
{
  tibrv_status status;
  char         dictInBox[ 256 ];
  tibrvMsg     m;

  this->timeoutSecs = timeoutSecs;
  if ( dictSubject == NULL )
    dictSubject = "_TIC.REPLY.SASS.DATA.DICTIONARY";
  logDebug( LDEBUG, "Requesting dictionary over RV, subject %s", dictSubject );

  status = tibrvTransport_CreateInbox( this->session.rvT, dictInBox,
                                       sizeof( dictInBox ) );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
  status = tibrvEvent_CreateListener( &this->rvD, this->session.xQ,
                                      RaiDict_tibrv::onMsg, this->session.rvT,
                                      dictInBox, this );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  status = tibrvMsg_Create( &m );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
  status = tibrvMsg_AddU32( m, "flags", 1 );
  if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );

  if ( (status = tibrvMsg_SetSendSubject( m, dictSubject )) != TIBRV_OK ||
       (status = tibrvMsg_SetReplySubject( m, dictInBox )) != TIBRV_OK ||
       (status = tibrvTransport_Send( this->session.rvT, m )) != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );

  this->startTime = Time::currentTimeMillisecs();

  if ( loadWait ) {
    while ( this->InProgress() )
      ;
    if ( ! this->HaveDict() )
      throw RaiApiErr::getErr( RaiApiErr::BAD_DICT );
  }
}


bool
RaiDict_tibrv::InProgress( void )
{
  if ( this->complete )
    return false;
  /*tibrvQueue_TimedDispatch( this->session.xQ, 0.01 );*/
  Time::sleepMillisecs( 10 );
  return this->startTime + (TimeMSecs) ( this->timeoutSecs * 1000 ) >
         Time::currentTimeMillisecs();
}


RaiTimer_tibrv::~RaiTimer_tibrv()
{
  if ( this->rvE != TIBRV_INVALID_ID )
    tibrvEvent_Destroy( this->rvE );
}


void
RaiTimer_tibrv::onTimer( tibrvEvent /* I */,  tibrvMsg /* msg */,  void *cl )
{
  RaiTimer_tibrv & me = *(RaiTimer_tibrv *) cl;

  try {
    me.cb.onTimer( me, me.cl );
  } catch ( RaiException e ) {
    logError( LERROR, e, "Timer callback" );
  }
}


void
RaiTimer_tibrv::Start( void )
{
  tibrv_f64    rv_interval;
  tibrv_status status;

  if ( this->rvE != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->rvE );
    this->rvE = TIBRV_INVALID_ID;
  }
  rv_interval = (double) this->ival / 1000.0;
  status      = tibrvEvent_CreateTimer( &this->rvE, this->q.rvQ,
                                        RaiTimer_tibrv::onTimer, rv_interval,
                                        this );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
}


void
RaiTimer_tibrv::Stop( void )
{
  if ( this->rvE != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->rvE );
    this->rvE = TIBRV_INVALID_ID;
  }
}


TimeMSecs
RaiTimer_tibrv::GetInterval( void )
{
  return this->ival;
}


void
RaiTimer_tibrv::SetInterval( TimeMSecs interval )
{
  tibrv_f64    rv_interval;
  tibrv_status status;

  this->ival = interval;
  if ( this->rvE != TIBRV_INVALID_ID ) {
    rv_interval = (double) interval / 1000.0;
    status = tibrvEvent_ResetTimerInterval( this->rvE, rv_interval );
    if ( status != TIBRV_OK )
      throw RaiApi_tibrv::getRvErr( status );
  }
}


RaiSubscribe_tibrv::~RaiSubscribe_tibrv()
{
  if ( this->inList ) {
    this->q.apiLock->lock();
    this->q.rmSubList( this );
    this->q.subClock++;
    this->q.apiLock->unlock();
  }
  if ( this->snapId != TIBRV_INVALID_ID )
    tibrvEvent_Destroy( this->snapId );
  if ( this->listenId != TIBRV_INVALID_ID )
    tibrvEvent_Destroy( this->listenId );
  if ( this->subject != NULL )
    FREE( this->subject );
  if ( this->extra != NULL )
    delete this->extra;
}


void
RaiSubscribe_tibrv::onMsg( tibrvEvent id,  tibrvMsg msg,  void *cl )
{
  RaiSubscribe_tibrv & me = *(RaiSubscribe_tibrv *) cl;
  RaiSubject      subj2;
  const void    * bufp;
  const char    * subj;
  tibrv_u32       msgSize;
  tibrv_status    status;

  if ( (status = tibrvMsg_GetByteSize( msg, &msgSize )) == TIBRV_OK )
    status = tibrvMsg_GetAsBytes( msg, &bufp );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Getting message bytes" );
    return;
  }
/*  status = tibrvMsg_GetOpaque( msg, "_data_", &bufp, &msgSize );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Getting message bytes" );
    return;
  }*/
  status = tibrvMsg_GetSendSubject( msg, &subj );
  if ( status != TIBRV_OK ) {
    RaiException e = RaiApi_tibrv::getRvErr( status );
    logError( LERROR, e, "Getting send subject" );
    return;
  }

  status = tibrvMsg_GetReplySubject( msg, &me.reply );
  if ( status != TIBRV_OK )
    me.reply = NULL;
  try {
    Rai_u16  msgType   = SassConst::MAX_TYPE,
             recType   = 0,
             seqNo     = 0,
             recStatus = SassConst::MAX_STATUS;
    RaiMsg   raiMsg;
    RaiField field;
    raiMsg.UnPack( RV_PROTO, (RaiMsg_data) bufp, msgSize,
                   RAIMSG_MEMORY_STATIC );
    if ( field.First( &raiMsg ) && field.NameSize() > 4 &&
         field.Name()[ 0 ] == '_' ) {
      switch ( field.Name()[ 1 ] ) {
        case 'd': if ( ! field.isNamed( "_data_" ) ) goto not_opaque; break;
        case 'Q': if ( ! field.isNamed( "_QFORM" ) ) goto not_opaque; break;
        case 'T': if ( ! field.isNamed( "_TIBMSG" ) ) goto not_opaque; break;
        case 'R': if ( ! field.isNamed( "_RAIMSG" ) ) goto not_opaque; break;
        default: goto not_opaque;
      }
      bufp    = field.Data();
      msgSize = field.Size();
      raiMsg.UnPack( (RaiMsg_data *) bufp, msgSize );
    not_opaque:;
    }
    raiMsg.GetSassHeader( msgType, recType, seqNo, recStatus );
    me.q.apiLock->lock();
    RaiSubscribe::RaiSubState ostate = me.state,
                              rstate = SassToSubState( msgType, recStatus );
    me.state = NewState( ostate, rstate );
    RaiMsgEvent event( me, subj,
                  ( id == me.snapId ? RaiMsgEvent::SNAP : RaiMsgEvent::UPDATE ),
                    msgType, recStatus, ostate, rstate, me.state, 0, 0, 0, 0 );
    me.count++;
    // dna - check content based entitlements here 
    if ( RaiApi::contentEntitle( &raiMsg ) == false ) {
      logDebug( LDEBUG, "failed content entitlement test" );
      // should cancel subscription now 
    }
    else {
      try {
        me.cb.onMsg( event, raiMsg, me.cl );
      } catch ( Error e ) {
        logError( LERROR, e, "Callback exception" );
      }
    }
    me.q.apiLock->unlock();
  } catch ( Error e ) {
    logError( LERROR, e, "Unpack message failed" );
  }
#if 0
  /* if wildcard, may have more than one reply */
  if ( me.state != STATE_WILDCARD && id == me.snapId ) {
    tibrvEvent_Destroy( me.snapId );
    me.snapId = TIBRV_INVALID_ID;
  }
#endif
}


void
RaiSubscribe_tibrv::Start( const char *subject,  RaiSubParameter parm,
                           Rai_u32 timeoutMSecs )
{
  tibrv_status status;
  const char * p;

  if( subject == NULL )
    throw RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT );
  if ( this->InProgress() )
    this->Cancel();

  STRDUP( this->subject, subject );
  if ( ( (byte) parm & NO_PREFIX ) != 0 )
    this->usePrefix = true;
  else
    this->usePrefix = false;
  parm = (RaiSubParameter) ( (byte) parm & BOTH );
  this->subType = parm;
  /* check if wildcard present */
  this->state = STATE_NO_MSG;
  if ( (p = ::strchr( subject, '>' )) != NULL ) {
    if ( p == subject || *(p - 1) == '.' ) {
      if ( *(p + 1) == '\0' )
        this->state = STATE_WILDCARD;
    }
  }
  if ( this->state != STATE_WILDCARD &&
       (p = ::strchr( subject, '*' )) != NULL ) {
    if ( p == subject || *(p - 1) == '.' ) {
      if ( *(p + 1) == '.' || *(p + 1) == '\0' )
        this->state = STATE_WILDCARD;
    }
  }
  /* dna - entitlement subscription checking */
  if ( RaiApi::canSubscribe( subject ) == false ) {
    logDebug( LDEBUG, "entitle check failed" );
    throw RaiApiErr::getErr( RaiApiErr::NO_PERMISSION );
  }

  this->q.apiLock->lock();
  try {
    this->q.addSubList( this );
    this->q.subClock++;
    if ( timeoutMSecs != 0 ) {
      if ( parm != UPDATE && this->state != STATE_WILDCARD &&
           ! this->hasTimer ) {
        RaiSubscribeTimer_tibrv *t = NEW RaiSubscribeTimer_tibrv( *this );
        tibrvId timerId;
        this->hasTimer = true;
        tibrvEvent_CreateTimer( &timerId, this->q.session.xQ,
                                RaiSubscribeTimer_tibrv::onTimer,
                                (double) timeoutMSecs / 1000.0, t );
      }
    }
    this->sendSubRequest();

    if ( parm != SNAP ) {
      /* listen for subject */
      status = tibrvEvent_CreateListener( &this->listenId, this->q.rvQ,
                                          RaiSubscribe_tibrv::onMsg,
                                          this->q.session.rvT, subject, this );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );
    } /* all cases except SNAP */
  } catch ( RaiException e ) {
    logError( LERROR, e, "%s subject \"%s\"", subject,
              parm == BOTH ? "Subscribe" : 
              ( parm == SNAP ? "Snapshot" : "Listen" ) );
    throw e;
  }
  this->q.apiLock->unlock();
}


static void
convertToRvMsg( RaiMsg &rvmsg,  RaiMsg &raimsg )
{
  RaiField field;
  if ( field.First( &raimsg ) ) {
    do {
      if ( field.Type() != RAIMSG_MESSAGE )
        rvmsg.Append( &field );
      else {
        RaiMsg rvmsg2( RV_PROTO );
        convertToRvMsg( rvmsg2, *(RaiMsg *) field.Data() );
        rvmsg.Append( field.Name(), &rvmsg2 );
      }
    } while ( field.Next() );
  }
}


void
RaiSubscribe_tibrv::sendSubRequest( void )
{
  char            snapSubjectName[ SassConst::MAX_SUBJECT_LEN ],
                  inboxName[ 256 ];
  RaiSubParameter parm = (RaiSubParameter) this->subType;
  tibrvMsg        m;
  tibrv_status    status;

  if ( parm != UPDATE ) {
    if ( this->snapId == TIBRV_INVALID_ID ) {
      /* create a snap/initial reply inbox */
      status = tibrvTransport_CreateInbox( this->q.session.rvT, inboxName,
                                           sizeof( inboxName ) );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );

      status = tibrvEvent_CreateListener( &this->snapId, this->q.rvQ,
                                          RaiSubscribe_tibrv::onMsg,
                                          this->q.session.rvT, inboxName, this);
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );
    }
    if ( this->extra != NULL ) {
      if ( this->extra->GetProtocol() != RV_PROTO ) {
        RaiMsg   rvmsg( RV_PROTO );
        convertToRvMsg( rvmsg, *this->extra );
        status = tibrvMsg_CreateFromBytes( &m, rvmsg.Packed() );
      }
      else {
        status = tibrvMsg_CreateFromBytes( &m, this->extra->Packed() );
      }
    }
    else {
      /* send a message to _SNAP.subject for request  */
      status = tibrvMsg_Create( &m );
    }
    if ( status != TIBRV_OK )
      throw RaiApi_tibrv::getRvErr( status );

    if ( this->usePrefix == false && this->q.session.api.snapPrefix != NULL ) {
      unsigned int len = ::strlen( this->q.session.api.snapPrefix );
      if ( len >= sizeof( snapSubjectName ) )
        len = sizeof( snapSubjectName ) - 1;
      ::memcpy( snapSubjectName, this->q.session.api.snapPrefix, len );
      str_copy( &snapSubjectName[ len ], subject,
                sizeof( snapSubjectName ) - len );
    }
    else {
      str_copy( snapSubjectName, subject, sizeof( snapSubjectName ) );
    }
    try {
      status = tibrvMsg_SetSendSubject( m, snapSubjectName );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );

      status = tibrvMsg_SetReplySubject( m, inboxName );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );

      Rai_u16 flags = ( parm == BOTH ? ( SassConst::SUBSCRIBE_FLAG |
                                         SassConst::INITIAL_VALUES_FLAG )
                                     : SassConst::SNAPSHOT_FLAG );
      /* if we get here and are checking entitlements set the entitlement flag */
      //if ( RaiApi::entitleImpl->haveLoggedIn == true )
        //flags |= SassConst::ENTITLED_FLAG ; 
      status = tibrvMsg_AddU16( m, "flags", flags );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );

      status = tibrvTransport_Send( this->q.session.rvT, m );
      if ( status != TIBRV_OK )
        throw RaiApi_tibrv::getRvErr( status );
    } catch ( ... ) {
      tibrvMsg_Destroy( m );
      throw;
    }
    tibrvMsg_Destroy( m );
  }
}


void
RaiSubscribe_tibrv::Refresh( Rai_u32 timeoutMSecs )
{
  RaiException e2 = NULL;

  this->q.apiLock->lock();
  if ( this->snapId != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->snapId );
    this->snapId = TIBRV_INVALID_ID;
  }

  try {
    if ( timeoutMSecs != 0 ) {
      if ( this->state != STATE_WILDCARD && ! this->hasTimer ) {
        RaiSubscribeTimer_tibrv *t = NEW RaiSubscribeTimer_tibrv( *this );
        tibrvId timerId;
        this->hasTimer = true;
        tibrvEvent_CreateTimer( &timerId, this->q.session.xQ,
                                RaiSubscribeTimer_tibrv::onTimer,
                                (double) timeoutMSecs / 1000.0, t );
      }
    }
    this->sendSubRequest();
  } catch ( RaiException e ) {
    logError( LERROR, e, "%s subject \"%s\"", this->subject,
              this->subType == (byte) BOTH ? "Subscribe" : 
              ( this->subType == (byte) SNAP ? "Snapshot" : "Listen" ) );
    e2 = e;
  }
  this->q.apiLock->unlock();
  if ( e2 != NULL )
    throw e2;
}


void
RaiSubscribeTimer_tibrv::onTimer( tibrvEvent I,  tibrvMsg /* msg */,  void *cl )
{
  RaiSubscribeTimer_tibrv * me = (RaiSubscribeTimer_tibrv *) cl;
  RaiQueue_tibrv     * q;
  RaiSubscribe_tibrv * s = NULL;

  tibrvEvent_Destroy( (tibrvEvent) I );

  me->session.lock->lock();
  for ( q = me->session.qHd; q != NULL; q = q->next )
    if ( q == &me->q )
      break;
  me->session.lock->unlock();

  if ( q != NULL ) {
    me->q.apiLock->lock();
    for ( s = me->q.subHd; s != NULL; s = s->next )
      if ( s == &me->sub )
        break;
    if ( s != NULL ) {
      s->hasTimer = false;
      if ( s->count != me->count )
        s = NULL;
    }
    if ( s != NULL ) {
      RaiSubscribe::RaiSubState recvState;
      static const Rai_u16 msgType   = SassConst::TRANSIENT,
                           recStatus = SassConst::STATUS_TIMEOUT;
      RaiMsg msg;

      try {
        create_status_msg( msg, recvState, msgType, recStatus );
        RaiSubscribe::RaiSubState oldState = s->state;
        s->state = RaiSubscribe::NewState( oldState, recvState );
        RaiMsgEvent ev( *s, s->subject, RaiMsgEvent::UPDATE, msgType,
                        recStatus, oldState, recvState, s->state, 0, 0, 0, 0 );
        s->cb.onMsg( ev, msg, s->cl );
      } catch ( Error e ) {
        logError( LERROR, e, "Subscribe onTimer" );
      }
    }
    me->q.apiLock->unlock();
  }
  delete me;
}


void
RaiSubscribe_tibrv::Cancel( void )
{
  this->q.apiLock->lock();
  this->q.rmSubList( this );
  this->q.subClock++;
  if ( this->snapId != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->snapId );
    this->snapId = TIBRV_INVALID_ID;
  }
  if ( this->listenId != TIBRV_INVALID_ID ) {
    tibrvEvent_Destroy( this->listenId );
    this->listenId = TIBRV_INVALID_ID;
  }
  this->q.apiLock->unlock();
}


const char *
RaiSubscribe_tibrv::Subject( void )
{
  return this->subject;
}


bool
RaiSubscribe_tibrv::InProgress( void )
{
  return this->snapId != TIBRV_INVALID_ID || this->listenId != TIBRV_INVALID_ID;
}


void
RaiSubscribe_tibrv::SetExtra( RaiMsg *ex )
{
  if ( this->extra != NULL ) {
    delete this->extra;
    this->extra = NULL;
  }
  if ( ex != NULL ) {
    RaiMsg *x = NEW RaiMsg();
    x->Copy( ex );
    this->extra = x;
  }
}


RaiPublish_tibrv::~RaiPublish_tibrv()
{
}

void
RaiPublish_tibrv::Publish( const char *subject,  const void *buffer,  
                           Rai_u32 size,  TimeNSecs /* stamp */,
                           Rai_u32 msgTypeId )
{
  tibrvMsg     m = NULL;
  tibrv_status status; 
  unsigned int len;
  char         subjBuf[ SassConst::MAX_SUBJECT_LEN ];

  if ( this->prefix != NULL ) {
    ::memcpy( subjBuf, this->prefix, this->prefixLen );
    len = ::strlen( subject ) + 1;
    if ( len > sizeof( subjBuf ) - this->prefixLen )
      len = sizeof( subjBuf ) - this->prefixLen;
    ::memcpy( &subjBuf[ this->prefixLen ], subject, len );
    subject = subjBuf;
  }

  // dna -perform entitlement checking for publishing here 
  if ( RaiApi::canPublish( subject ) == false ) {
    logDebug( LDEBUG, "entitle check failed" );
    throw( RaiApiErr::getErr( RaiApiErr::NO_PERMISSION ) );
  }

  if ( size >= 8 &&
      ((byte *) buffer)[ 4 ] == 0x99 &&
      ((byte *) buffer)[ 5 ] == 0x55 &&
      ((byte *) buffer)[ 6 ] == 0xee &&
      ((byte *) buffer)[ 7 ] == 0xaa ) {
    status = tibrvMsg_CreateFromBytes( &m, buffer );
  }
  // wrap the message in a RVMsg and send it
  else if ( (status = tibrvMsg_Create( &m )) == TIBRV_OK ) {
    const char *s;
    switch ( msgTypeId ) {
      case RAIMSG_TYPE_ID:        s = "_TIBMSG"; break;
      case TIB_SASS_TYPE_ID:
      case TIB_SASS_FORM_TYPE_ID: s = "_QFORM"; break;
      default:                    s = "_data_"; break;
    }
    status = tibrvMsg_AddOpaque( m, s, buffer, size );
  }
  if ( status == TIBRV_OK )
    if ( (status = tibrvMsg_SetSendSubject( m, subject )) == TIBRV_OK )
      status = tibrvTransport_Send( this->session.rvT, m );

  if ( m != NULL )
    tibrvMsg_Destroy( m );
  if ( status != TIBRV_OK )
    throw RaiApi_tibrv::getRvErr( status );
}

void
RaiPublish_tibrv::Destroy( void )
{
}

static const char rv_info_sys_listen_start[] = "_RV.INFO.SYSTEM.LISTEN.START.",
                  rv_info_sys_listen_stop[] = "_RV.INFO.SYSTEM.LISTEN.STOP.",
                  rv_snap[] = "_SNAP.";

void
RaiInteractivePublish_tibrv::onMsg( RaiMsgEvent &event,  RaiMsg &msg,  void * /* cl */ )
{
  unsigned int off, flags;
  const char *reply;

  if ( &event.subscribe == this->inSub ) {
    off = sizeof( rv_info_sys_listen_start ) - 1;
    flags = SassConst::SUBSCRIBE_FLAG;
    if ( (reply = this->inSub->reply) != NULL )
      flags |= SassConst::INITIAL_VALUES_FLAG;
  }
  else if ( &event.subscribe == this->inCxl ) {
    off = sizeof( rv_info_sys_listen_stop ) - 1;
    flags = SassConst::UNSUBSCRIBE_FLAG;
    reply = NULL;
  }
  else if ( &event.subscribe == this->inSnp ) {
    off = sizeof( rv_snap ) - 1;
    flags = SassConst::SNAPSHOT_FLAG;
    reply = this->inSnp->reply;
    msg.Get( "flags", flags );
  }
  else {
    off   = 0;
    flags = 0;
    reply = NULL;
  }
  if ( off > ::strlen( event.subject ) )
    off = 0;

  RaiSubscribeEvent subEvent( *this, &event.subject[ off ], reply, flags );
  this->cb.onSubscribe( subEvent, msg, NULL );
}

void
RaiInteractivePublish_tibrv::InteractiveStart(  const char *subject )
{
  char subBuf[ SassConst::MAX_SUBJECT_LEN ];
  unsigned int len;

  logDebug( LDEBUG, "interactive publier start(%s)", subject );
  try {
    len = ::strlen( subject ) + 1;
    if ( len + sizeof( rv_info_sys_listen_stop ) <= sizeof( subBuf ) ) {
      // create a snapshot subject
      ::memcpy( subBuf, rv_snap, sizeof( rv_snap ) - 1 );
      ::memcpy( &subBuf[ sizeof( rv_snap ) - 1 ], subject, len );
      if ( this->inSnp == NULL )
        this->inSnp = NEW RaiSubscribe_tibrv( this->q, this, this );
      this->inSnp->Start( subBuf, RaiSubscribe::UPDATE, 0 );

      // create a subscriber to listen to the request subject
      ::memcpy( subBuf, rv_info_sys_listen_start,
                sizeof( rv_info_sys_listen_start ) - 1 );
      ::memcpy( &subBuf[ sizeof( rv_info_sys_listen_start ) - 1 ],
                subject, len );
      if ( this->inSub == NULL )
        this->inSub = NEW RaiSubscribe_tibrv( this->q, this, this );
      this->inSub->Start( subBuf, RaiSubscribe::UPDATE, 0 );

      // create a subscriber to listen to the cancel subject
      ::memcpy( subBuf, rv_info_sys_listen_stop,
                sizeof( rv_info_sys_listen_stop ) - 1 );
      ::memcpy( &subBuf[ sizeof( rv_info_sys_listen_stop ) - 1 ],
                subject, len );
      if ( this->inCxl == NULL )
        this->inCxl = NEW RaiSubscribe_tibrv( this->q, this, this );
      this->inCxl->Start( subBuf, RaiSubscribe::UPDATE, 0 );
    }
  }
  catch ( RaiException ){
     // clean up and throw again  
  }
}


bool
RaiInteractivePublish_tibrv::InProgress( void )
{
  return false;
}


void
RaiInteractivePublish_tibrv::Destroy( void )
{

}


void
RaiInteractivePublish_tibrv::Publish( const char *subject,  const void *buffer,  
                                      Rai_u32 size,  TimeNSecs stamp,
                                      Rai_u32 msgTypeId )
{
  this->RaiPublish_tibrv::Publish( subject, buffer, size, stamp, msgTypeId );
}


void
RaiInteractivePublish_tibrv::InteractiveCancel( void )
{
  if ( this->inCxl != NULL )
    this->inCxl->Cancel();
  if ( this->inSub != NULL )
    this->inSub->Cancel();
  if ( this->inSnp != NULL )
    this->inSnp->Cancel();
}


RaiInteractivePublish_tibrv::~RaiInteractivePublish_tibrv()
{
  if ( this->inCxl != NULL )
    delete this->inCxl;
  if ( this->inSub != NULL )
    delete this->inSub;
  if ( this->inSnp != NULL )
    delete this->inSnp;
}


RaiException
RaiApi_tibrv::getRvErr( tibrv_status status )
{
  static const char mod[] = "RaiApi_tibrv";
  static ErrorRec e[ 102 ];
  static const unsigned int numErrs = sizeof( e ) / sizeof( e[ 0 ] ) - 1;

  if ( status >= 0 && (unsigned int) status < numErrs ) {
    if ( e[ status ].module == NULL ) {
      e[ status ].status = (unsigned int) status;
      e[ status ].reason = tibrvStatus_GetText( status );
      if ( e[ status ].reason == NULL )
        e[ status ].reason = "null";
      e[ status ].module = mod;
    }
    return &e[ status ];
  }

  if ( e[ numErrs ].module == NULL ) {
    e[ numErrs ].status = 999;
    e[ numErrs ].reason = "RV7 call failed";
    e[ numErrs ].module = mod;
  }

  return &e[ numErrs ];
}

