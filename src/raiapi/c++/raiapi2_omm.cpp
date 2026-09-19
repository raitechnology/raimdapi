/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 *
 * raiapi2_omm.cpp -- the RaiApi over the OMM consumer api (omm/ommapi.h),
 * the way raiapi2_tibrv.cpp sits on the tibrv 7 api.  Selected with
 * -api omm.
 *
 * Subjects are SERVICE.SECTOR.RIC (e.g. IDN_RDF.REC.IBM.N); the service and
 * sector are matched against the source directory the transport downloads
 * at login, REC is the market price domain.  Messages arrive as the sass-form
 * RVMSG the api converts from RWF (MSG_TYPE / SEQ_NO / REC_STATUS + fields
 * named by the RDM dictionary), so everything downstream of the callback is
 * the same as with the tibrv transport.
 *
 * v1 is consumer only: Publish() and interactive publish raise
 * UNSUPPORTED_MTHD. */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_api__raiapi2_omm_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#include <omm/ommapi.h>
#include "raiapi2.h"
#include "base/log.h"
#include "base/rai_factory.h"
#include "base/thread.h"
#include "util/args.h"
#include "msg/sass_const.h"
#include "stream/byte_array_stream.h"
#include "util/str_util.h"

using namespace rai;

extern "C" RAI_DLL_EXPORT RaiApi *RaiApi_RaiOpen_omm( int argc,  char *argv[] );

struct RaiQueue_omm;
struct RaiSubscribe_omm;

struct RaiApi_omm : public RaiApi {
  char * daemon,     /* host[:port] */
       * appName,
       * rdmPath;    /* RDM dictionary cfiles, NULL = download */
  bool   noDict;

  SYS_OPS( RaiApi_omm );
  RaiApi_omm() : daemon( 0 ), appName( 0 ), rdmPath( 0 ), noDict( false ) {}
  virtual ~RaiApi_omm();

  virtual const char *GetApiName( void ) { return "omm"; }
  virtual void GetArgs( Args &args );
  virtual void ParseArgs( Args &args );
  virtual RaiSession *CreateSession( void );
  virtual void Close( void );
  virtual bool SetIoctl( const char *parameter,  const void *value );
  virtual bool GetIoctl( const char *parameter,  void *value );

  static RaiException getOmmErr( omm_status status );
};

struct RaiSession_omm : public RaiSession, public Thread {
  RaiApi_omm          & api;
  ommTransport          tport;
  ommQueue              xQ;        /* transport state events + sub timers */
  ommEvent              stateEv;
  Mutex               * lock;
  RaiQueue_omm        * qHd, * qTl;
  char                * transportName;
  RaiDataLossCallback * dataLossCb;
  void                * dataLossCl;
  bool                  quit;

  SYS_OPS( RaiSession_omm );
  RaiSession_omm( RaiApi_omm *a ) : Thread( "ommqueue" ),
    api( *a ), tport( 0 ), xQ( 0 ), stateEv( 0 ), lock( 0 ), qHd( 0 ),
    qTl( 0 ), transportName( 0 ), dataLossCb( 0 ), dataLossCl( 0 ),
    quit( false ) {}
  virtual ~RaiSession_omm();

  /* RaiSession */
  virtual void Start( void );
  virtual RaiQueue *CreateQueue( bool direct );
  virtual RaiPublish *CreatePublish( bool autoInc );
  virtual RaiDict *CreateDict( void );
  virtual void Destroy( void );
  virtual RaiEntitlement *Login( const char *user );
  virtual void SetDataLossCB( RaiDataLossCallback *cb,  void *closure );
  virtual void NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus );
  virtual RaiApi *GetApi( void ) { return &this->api; }

  /* Thread */
  virtual void run( void );

  static void state_onMsg( ommEvent ev,  ommMsg msg,  void *cl );
  static void quit_onTimer( ommEvent ev,  ommMsg msg,  void *cl );

  void addQueueList( RaiQueue_omm *q );
  void rmQueueList( RaiQueue_omm *q );
};

struct RaiQueue_omm : public RaiQueue {
  RaiSession_omm     & session;
  ommQueue             ommQ;
  RaiQueue_omm       * next, * back;
  RaiSubscribe_omm   * subHd, * subTl;
  Mutex              * apiLock;
  unsigned int         matchClock,
                       subClock;
  bool                 inList,
                       isDestroyed;

  SYS_OPS( RaiQueue_omm );
  RaiQueue_omm( RaiSession_omm &s,  ommQueue q )
    : session( s ), ommQ( q ), next( 0 ), back( 0 ), subHd( 0 ), subTl( 0 ),
      apiLock( 0 ), matchClock( 0 ), subClock( 0 ), inList( false ),
      isDestroyed( false ) {}
  virtual ~RaiQueue_omm();

  /* RaiQueue */
  virtual RaiTimer *CreateTimer( RaiTimerCallback *cb,  void *closure );
  virtual RaiSubscribe *CreateSubscribe( RaiMsgCallback *cb,  void *closure );
  virtual RaiInteractivePublish *CreateInteractivePublish(
                                      RaiSubscribeCallback *cb,
                                      void *closure );
  virtual void NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus );
  struct AppEvent {
    ommEvent         ev;
    RaiAppCallback * cb;
    void           * eventData;
    Rai_i32          eventEnum;

    SYS_OPS( AppEvent );
    AppEvent( RaiAppCallback *c,  void *ed,  Rai_i32 en )
      : ev( OMM_INVALID_ID ), cb( c ), eventData( ed ), eventEnum( en ) {}
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

  void addSubList( RaiSubscribe_omm *s );
  void rmSubList( RaiSubscribe_omm *s );

  static void onAppTimer( ommEvent ev,  ommMsg msg,  void *cl );
};

struct RaiDict_omm : public RaiDict {
  RaiSession_omm & session;
  TimeMSecs        startTime;
  Rai_u32          timeoutSecs;
  bool             complete;

  SYS_OPS( RaiDict_omm );
  RaiDict_omm( RaiSession_omm &s ) : session( s ), startTime( 0 ),
    timeoutSecs( 0 ), complete( false ) {}
  virtual ~RaiDict_omm() {}

  /* RaiDict: the transport downloads the RDM dictionary at login, Load()
   * just waits for it */
  virtual void Load( Rai_u32 timeoutSecs,  const char *dictSubject,
                     bool loadWait );
  virtual bool HaveDict( void ) { return this->complete; }
  virtual bool InProgress( void );
  virtual RaiSession *GetSession( void ) { return &this->session; }
};

struct RaiTimer_omm : public RaiTimer {
  RaiQueue_omm     & q;
  RaiTimerCallback & cb;
  TimeMSecs          ival;
  void             * cl;
  ommEvent           ev;

  SYS_OPS( RaiTimer_omm );
  RaiTimer_omm( RaiQueue_omm &qu,  RaiTimerCallback *c,  void *closure )
    : q( qu ), cb( *c ), ival( 0 ), cl( closure ), ev( OMM_INVALID_ID ) {}
  virtual ~RaiTimer_omm();

  virtual void Start( void );
  virtual void Stop( void );
  virtual TimeMSecs GetInterval( void );
  virtual void SetInterval( TimeMSecs interval );
  virtual RaiQueue *GetQueue( void ) { return &this->q; }

  static void onTimer( ommEvent ev,  ommMsg msg,  void *cl );
};

struct RaiSubscribe_omm : public RaiSubscribe {
  RaiQueue_omm       & q;
  RaiMsgCallback     & cb;
  void               * cl;
  char               * subject;
  RaiSubscribe_omm   * next, * back;
  ommEvent             listenId;
  unsigned int         match,
                       count;
  byte                 subType;
  bool                 inList,
                       hasTimer;

  SYS_OPS( RaiSubscribe_omm );
  RaiSubscribe_omm( RaiQueue_omm &qu,  RaiMsgCallback *c,  void *closure )
    : q( qu ), cb( *c ), cl( closure ), subject( 0 ), next( 0 ), back( 0 ),
      listenId( OMM_INVALID_ID ), match( 0 ), count( 0 ), subType( 0xff ),
      inList( false ), hasTimer( false ) {}
  virtual ~RaiSubscribe_omm();

  virtual void Start( const char *subject,  RaiSubParameter parm,
                      Rai_u32 timeoutMSecs );
  void startListener( void );
  void startTimer( Rai_u32 timeoutMSecs );
  virtual void Cancel( void );
  virtual void Refresh( Rai_u32 timeoutMSecs );
  virtual const char *Subject( void );
  virtual bool InProgress( void );
  virtual RaiQueue *GetQueue( void ) { return &this->q; }
  virtual void SetExtra( RaiMsg * ) {} /* no request payload with OMM */

  static void onMsg( ommEvent ev,  ommMsg msg,  void *cl );
};

struct RaiSubscribeTimer_omm {
  RaiSession_omm   & session;
  RaiQueue_omm     & q;
  RaiSubscribe_omm & sub;
  unsigned int       count;

  SYS_OPS( RaiSubscribeTimer_omm );
  RaiSubscribeTimer_omm( RaiSubscribe_omm & s ) : session( s.q.session ),
     q( s.q ), sub( s ), count( s.count ) {}

  static void onTimer( ommEvent ev,  ommMsg msg,  void *cl );
};

struct RaiPublish_omm : public RaiPublish {
  RaiSession_omm & session;

  SYS_OPS( RaiPublish_omm );
  RaiPublish_omm( RaiSession_omm &s,  bool autoInc )
    : RaiPublish( autoInc ), session( s ) {}
  virtual ~RaiPublish_omm() {}

  virtual void Publish( const char *subject,  const void *buffer,
                        Rai_u32 size,  TimeNSecs stamp,  Rai_u32 msgTypeId );
  virtual void Destroy( void ) {}
  virtual RaiSession *GetSession( void ) { return &this->session; }
};

/* ---- api ------------------------------------------------------------------ */

RAI_DLL_EXPORT RaiApi *
RaiApi_RaiOpen_omm( int /* argc */,  char * /* argv */[] )
{
  omm_status status = omm_Open();
  if ( status != OMM_OK ) {
    Error e = RaiApi_omm::getOmmErr( status );
    logError( LERROR, e, "omm_Open() failed" );
  }
  return NEW RaiApi_omm();
}

RaiApi_omm::~RaiApi_omm()
{
  if ( this->daemon != NULL )
    FREE( this->daemon );
  if ( this->appName != NULL )
    FREE( this->appName );
  if ( this->rdmPath != NULL )
    FREE( this->rdmPath );
}

static const char daemon_arg[]   = "daemon",
                  app_name_arg[] = "appName",
                  rdm_path_arg[] = "rdmPath",
                  no_dict_arg[]  = "noRdmDict";

void
RaiApi_omm::GetArgs( Args &args )
{
  static StringArg daemon( daemon_arg, NULL, "<host[:port]>",
                           "OMM provider to connect to (127.0.0.1:14002)" );
  static StringArg appName( app_name_arg, NULL, "<name>",
                            "OMM login ApplicationName" );
  static StringArg rdmPath( rdm_path_arg, NULL, "<path>",
                            "Load the RDM dictionary from these files "
                            "instead of downloading it" );
  static BoolArg   noDict( no_dict_arg, false, "<bool>",
                           "Don't download the RDM dictionary" );

  this->RaiApi::GetArgs( args );
  args.add( &daemon, COMMAND_ARG | RESOURCE_ARG );
  args.add( &appName, COMMAND_ARG | RESOURCE_ARG );
  args.add( &rdmPath, COMMAND_ARG | RESOURCE_ARG );
  args.add( &noDict, COMMAND_ARG | RESOURCE_ARG );
}

void
RaiApi_omm::ParseArgs( Args &args )
{
  const char *s;

  this->RaiApi::ParseArgs( args );
  s = args.getString( daemon_arg );
  if ( s != NULL )
    STRDUP( this->daemon, s );
  s = args.getString( app_name_arg );
  if ( s != NULL )
    STRDUP( this->appName, s );
  s = args.getString( rdm_path_arg );
  if ( s != NULL )
    STRDUP( this->rdmPath, s );
  this->noDict = args.getBoolean( no_dict_arg );
}

void
RaiApi_omm::Close( void )
{
  omm_Close();
}

bool
RaiApi_omm::SetIoctl( const char *parameter,  const void *value )
{
  if ( ::strcmp( parameter, daemon_arg ) == 0 )
    STRDUP( this->daemon, (const char *) value );
  else if ( ::strcmp( parameter, app_name_arg ) == 0 )
    STRDUP( this->appName, (const char *) value );
  else if ( ::strcmp( parameter, rdm_path_arg ) == 0 )
    STRDUP( this->rdmPath, (const char *) value );
  else
    return this->RaiApi::SetIoctl( parameter, value );
  return true;
}

bool
RaiApi_omm::GetIoctl( const char *parameter,  void *value )
{
  if ( ::strcmp( parameter, daemon_arg ) == 0 )
    *(char **) value = this->daemon;
  else if ( ::strcmp( parameter, app_name_arg ) == 0 )
    *(char **) value = this->appName;
  else if ( ::strcmp( parameter, rdm_path_arg ) == 0 )
    *(char **) value = this->rdmPath;
  else
    return this->RaiApi::GetIoctl( parameter, value );
  return true;
}

/* ---- session -------------------------------------------------------------- */

RaiSession *
RaiApi_omm::CreateSession( void )
{
  RaiSession_omm * session;
  omm_status       status;
  char             buf[ 1024 ];
  ByteArrayOutputStream bout( (byte *) buf, sizeof( buf ) - 1 );

  session = NEW RaiSession_omm( this );
  session->lock = Mutex::create();
  status = ommQueue_Create( &session->xQ );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );

  bout.printf( "omm=%s", this->daemon != NULL ? this->daemon
                                              : "127.0.0.1:14002" );
  buf[ bout.length() ] = '\0';
  STRDUP( session->transportName, buf );
  return session;
}

/* transport state events (connected / disconnected) on the session queue */
void
RaiSession_omm::state_onMsg( ommEvent,  ommMsg msg,  void *cl )
{
  RaiSession_omm * session = (RaiSession_omm *) cl;
  ommMsgClass      cls;
  const char     * text;

  ommMsg_GetMsgClass( msg, &cls );
  ommMsg_GetStatusText( msg, &text );
  if ( cls == OMM_MSG_TRANSPORT_UP ) {
    logDebug( LDEBUG, "omm transport up: %s", text );
    if ( session->dataLossCb != NULL ) {
      RaiConnectionEvent event( *session );
      event.transportName      = session->transportName;
      event.description        = text;
      event.connectionOriented = true;
      event.isMulticast        = false;
      event.connectionCount    = 1;
      session->dataLossCb->onConnection( event, session->dataLossCl );
    }
  }
  else if ( cls == OMM_MSG_TRANSPORT_DOWN ) {
    logError( LERROR, NULL, "omm transport down: %s", text );
    if ( session->dataLossCb != NULL ) {
      RaiDataLossEvent event( *session );
      event.transportName      = session->transportName;
      event.description        = text;
      event.inboundPacketLoss  = 0;
      event.outboundPacketLoss = 0;
      event.connectionLoss     = true;
      event.isMulticast        = false;
      event.connectionCount    = 0;
      session->dataLossCb->onDataLoss( event, session->dataLossCl );
    }
  }
}

void
RaiSession_omm::Start( void )
{
  ommTransportParams parms;
  omm_status         status;
  const char       * user = NULL, * appId = NULL;

  this->api.GetIoctl( raiapi_userid_arg, &user );
  this->api.GetIoctl( raiapi_appid_arg, &appId );

  omm_InitTransportParams( &parms );
  parms.daemon        = this->api.daemon;
  parms.user          = user;
  parms.app_name      = this->api.appName != NULL ? this->api.appName : appId;
  parms.app_id        = NULL; /* "256" default; appid arg is the name */
  parms.dict_path     = this->api.rdmPath;
  parms.no_dictionary = this->api.noDict;

  status = ommTransport_Create( &this->tport, &parms );
  if ( status != OMM_OK ) {
    RaiException e = RaiApi_omm::getOmmErr( status );
    logError( LERROR, e, "Error in ommTransport_Create()" );
    throw e;
  }
  status = ommTransport_SetStateListener( this->tport, this->xQ,
                                          RaiSession_omm::state_onMsg, this,
                                          &this->stateEv );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );

  this->Thread::start();

  /* like the rv transport, Start() returns connected; the api keeps
   * reconnecting if the provider is not there yet */
  status = ommTransport_WaitConnected( this->tport, 10.0 );
  if ( status != OMM_OK ) {
    RaiException e = RaiApi_omm::getOmmErr( status );
    logError( LERROR, e, "Waiting for omm login/directory from %s",
              this->transportName );
  }
}

void
RaiSession_omm::addQueueList( RaiQueue_omm *q )
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
RaiSession_omm::rmQueueList( RaiQueue_omm *q )
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
RaiSession_omm::quit_onTimer( ommEvent ev,  ommMsg,  void *cl )
{
  RaiSession_omm *me = (RaiSession_omm *) cl;
  ommEvent_Destroy( ev );
  me->quit = true;
}

void
RaiSession_omm::run( void )
{
  omm_status status;
  while ( ! this->quit ) {
    status = ommQueue_TimedDispatch( this->xQ, 1.0 );
    if ( status != OMM_OK && status != OMM_TIMEOUT ) {
      RaiException e = RaiApi_omm::getOmmErr( status );
      logError( LERROR, e, "RaiSession dispatch thread" );
      break;
    }
  }
  this->exit();
}

RaiSession_omm::~RaiSession_omm()
{
  if ( this->transportName != NULL )
    FREE( this->transportName );
  if ( this->lock != NULL )
    delete this->lock;
}

RaiQueue *
RaiSession_omm::CreateQueue( bool /* direct */ )
{
  RaiQueue_omm * q;
  ommQueue       oq;
  omm_status     status;

  status = ommQueue_Create( &oq );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );
  q = NEW RaiQueue_omm( *this, oq );
  q->apiLock = Mutex::create( Mutex::RECURSIVE_LOCK );
  this->addQueueList( q );
  return q;
}

RaiPublish *
RaiSession_omm::CreatePublish( bool autoInc )
{
  return NEW RaiPublish_omm( *this, autoInc );
}

RaiDict *
RaiSession_omm::CreateDict( void )
{
  return NEW RaiDict_omm( *this );
}

void
RaiSession_omm::Destroy( void )
{
  if ( ! this->quit && this->isThreadRunning() ) {
    ommEvent timerId;
    ommEvent_CreateTimer( &timerId, this->xQ, RaiSession_omm::quit_onTimer,
                          0.0, this );
  }
  if ( ! this->isThreadJoined() )
    this->join();
  if ( this->stateEv != OMM_INVALID_ID ) {
    ommEvent_Destroy( this->stateEv );
    this->stateEv = OMM_INVALID_ID;
  }
  if ( this->xQ != 0 ) {
    ommQueue_Destroy( this->xQ );
    this->xQ = 0;
  }
  if ( this->tport != 0 ) {
    ommTransport_Destroy( this->tport );
    this->tport = 0;
  }
}

RaiEntitlement *
RaiSession_omm::Login( const char * /* user */ )
{
  return NULL;
}

void
RaiSession_omm::SetDataLossCB( RaiDataLossCallback *cb,  void *closure )
{
  this->dataLossCl = closure;
  this->dataLossCb = cb;
}

/* ---- queue ---------------------------------------------------------------- */

void
RaiQueue_omm::addSubList( RaiSubscribe_omm *s )
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
RaiQueue_omm::rmSubList( RaiSubscribe_omm *s )
{
  if ( s->inList ) {
    s->inList = false;
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

RaiTimer *
RaiQueue_omm::CreateTimer( RaiTimerCallback *cb,  void *closure )
{
  return NEW RaiTimer_omm( *this, cb, closure );
}

RaiSubscribe *
RaiQueue_omm::CreateSubscribe( RaiMsgCallback *cb,  void *closure )
{
  return NEW RaiSubscribe_omm( *this, cb, closure );
}

RaiInteractivePublish *
RaiQueue_omm::CreateInteractivePublish( RaiSubscribeCallback *,  void * )
{
  throw RaiApiErr::getErr( RaiApiErr::UNSUPPORTED_MTHD );
}

void
RaiQueue_omm::Destroy( void )
{
  this->apiLock->lock();
  this->isDestroyed = true;
  this->apiLock->unlock();
  if ( this->ommQ != 0 ) {
    ommQueue_Destroy( this->ommQ );
    this->ommQ = 0;
  }
  this->session.rmQueueList( this );
}

RaiQueue_omm::~RaiQueue_omm()
{
  if ( this->ommQ != 0 )
    this->Destroy();
  if ( this->apiLock != NULL )
    delete this->apiLock;
}

void
RaiQueue_omm::Mainloop( void )
{
  omm_status status;
  for (;;) {
    status = ommQueue_Dispatch( this->ommQ );
    if ( this->isDestroyed )
      return;
    if ( status != OMM_OK )
      throw RaiApi_omm::getOmmErr( status );
  }
}

void
RaiQueue_omm::TimedDispatch( Rai_u32 ivalMSecs )
{
  omm_status status;
  status = ommQueue_TimedDispatch( this->ommQ, (double) ivalMSecs / 1000.0 );
  if ( this->isDestroyed )
    return;
  if ( status != OMM_OK && status != OMM_TIMEOUT )
    throw RaiApi_omm::getOmmErr( status );
}

void
RaiQueue_omm::Dispatch( void )
{
  omm_status status;
  status = ommQueue_Dispatch( this->ommQ );
  if ( this->isDestroyed )
    return;
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );
}

Rai_u32
RaiQueue_omm::GetDepth( void )
{
  uint32_t   count;
  omm_status status = ommQueue_GetCount( this->ommQ, &count );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );
  return count;
}

static inline void
create_status_msg( RaiMsg &msg,  RaiSubscribe::RaiSubState &recvState,
                   Rai_u16 msgType,  Rai_u16 recStatus )
{
  recvState = RaiSubscribe::SassToSubState( msgType, recStatus );
  msg.Append( "MSG_TYPE", msgType );
  msg.Append( "REC_STATUS", recStatus );
}

void
RaiQueue_omm::NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus )
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
RaiQueue_omm::QueueEvent( RaiAppCallback *cb,  void *eventData,
                          Rai_i32 eventEnum,  Rai_u8 /* eventPriority */,
                          Rai_u32 expireMSecs )
{
  omm_status status;
  AppEvent * p = NEW AppEvent( cb, eventData, eventEnum );
  double     ival = ( expireMSecs != 0 ? (double) expireMSecs / 1000.0 : 0 );

  status = ommEvent_CreateTimer( &p->ev, this->ommQ, RaiQueue_omm::onAppTimer,
                                 ival, p );
  if ( status != OMM_OK ) {
    delete p;
    throw RaiApi_omm::getOmmErr( status );
  }
}

void
RaiQueue_omm::onAppTimer( ommEvent,  ommMsg,  void *cl )
{
  AppEvent * p = (AppEvent *) cl;
  try {
    p->cb->onAppEvent( p->eventData, p->eventEnum );
  } catch ( Error e ) {
    logError( LERROR, e, "onAppEvent()" );
  }
  ommEvent_Destroy( p->ev );
  delete p;
}

void
RaiSession_omm::NotifyStatus( Rai_u16 msgType,  Rai_u16 recStatus )
{
  RaiMsg msg;
  Error  e, e2;
  RaiSubscribe::RaiSubState recvState;
  RaiQueue_omm * q;

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
RaiQueue_omm::sendStatusMsg( RaiMsg &msg, RaiSubscribe::RaiSubState recvState,
                             Rai_u16 msgType,  Rai_u16 recStatus )
{
  RaiSubscribe_omm * sub;
  unsigned int       subClock;
  RaiException       e2 = NULL;

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

/* ---- dict ----------------------------------------------------------------- */

void
RaiDict_omm::Load( Rai_u32 timeoutSecs,  const char * /* dictSubject */,
                   bool loadWait )
{
  this->timeoutSecs = timeoutSecs;
  this->startTime   = Time::currentTimeMillisecs();
  this->complete    = false;
  logDebug( LDEBUG, "Waiting for the RDM dictionary from the omm provider" );
  if ( loadWait ) {
    while ( this->InProgress() )
      ;
    if ( ! this->HaveDict() )
      throw RaiApiErr::getErr( RaiApiErr::BAD_DICT );
  }
}

bool
RaiDict_omm::InProgress( void )
{
  int connected = 0, have = 0;
  if ( this->complete )
    return false;
  ommTransport_IsConnected( this->session.tport, &connected );
  ommTransport_HaveDictionary( this->session.tport, &have );
  if ( connected && have ) {
    this->complete = true;
    return false;
  }
  Time::sleepMillisecs( 10 );
  return this->startTime + (TimeMSecs) ( this->timeoutSecs * 1000 ) >
         Time::currentTimeMillisecs();
}

/* ---- timer ---------------------------------------------------------------- */

RaiTimer_omm::~RaiTimer_omm()
{
  if ( this->ev != OMM_INVALID_ID )
    ommEvent_Destroy( this->ev );
}

void
RaiTimer_omm::onTimer( ommEvent,  ommMsg,  void *cl )
{
  RaiTimer_omm & me = *(RaiTimer_omm *) cl;
  try {
    me.cb.onTimer( me, me.cl );
  } catch ( RaiException e ) {
    logError( LERROR, e, "Timer callback" );
  }
}

void
RaiTimer_omm::Start( void )
{
  omm_status status;
  if ( this->ev != OMM_INVALID_ID ) {
    ommEvent_Destroy( this->ev );
    this->ev = OMM_INVALID_ID;
  }
  status = ommEvent_CreateTimer( &this->ev, this->q.ommQ,
                                 RaiTimer_omm::onTimer,
                                 (double) this->ival / 1000.0, this );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );
}

void
RaiTimer_omm::Stop( void )
{
  if ( this->ev != OMM_INVALID_ID ) {
    ommEvent_Destroy( this->ev );
    this->ev = OMM_INVALID_ID;
  }
}

TimeMSecs
RaiTimer_omm::GetInterval( void )
{
  return this->ival;
}

void
RaiTimer_omm::SetInterval( TimeMSecs interval )
{
  this->ival = interval;
  if ( this->ev != OMM_INVALID_ID ) {
    omm_status status = ommEvent_ResetTimerInterval( this->ev,
                                                  (double) interval / 1000.0 );
    if ( status != OMM_OK )
      throw RaiApi_omm::getOmmErr( status );
  }
}

/* ---- subscribe ------------------------------------------------------------ */

RaiSubscribe_omm::~RaiSubscribe_omm()
{
  if ( this->inList ) {
    this->q.apiLock->lock();
    this->q.rmSubList( this );
    this->q.subClock++;
    this->q.apiLock->unlock();
  }
  if ( this->listenId != OMM_INVALID_ID )
    ommEvent_Destroy( this->listenId );
  if ( this->subject != NULL )
    FREE( this->subject );
}

/* a stream message: the api's sass-form conversion is a complete sass
 * message (header + fields), unpacked as RVMSG like the tibrv transport's */
void
RaiSubscribe_omm::onMsg( ommEvent,  ommMsg msg,  void *cl )
{
  RaiSubscribe_omm & me = *(RaiSubscribe_omm *) cl;
  const void       * bufp;
  uint32_t           msgSize;
  const char       * subj;
  ommMsgClass        cls;
  omm_status         status;

  ommMsg_GetSubject( msg, &subj );
  ommMsg_GetMsgClass( msg, &cls );
  status = ommMsg_GetSassMsg( msg, &bufp, &msgSize );
  if ( status != OMM_OK ) {
    RaiException e = RaiApi_omm::getOmmErr( status );
    logError( LERROR, e, "Converting %s message", subj );
    return;
  }
  try {
    Rai_u16  msgType   = SassConst::MAX_TYPE,
             recType   = 0,
             seqNo     = 0,
             recStatus = SassConst::MAX_STATUS;
    RaiMsg   raiMsg;
    raiMsg.UnPack( RV_PROTO, (RaiMsg_data) bufp, msgSize,
                   RAIMSG_MEMORY_STATIC );
    raiMsg.GetSassHeader( msgType, recType, seqNo, recStatus );
    me.q.apiLock->lock();
    RaiSubscribe::RaiSubState ostate = me.state,
                              rstate = SassToSubState( msgType, recStatus );
    me.state = NewState( ostate, rstate );
    RaiMsgEvent event( me, subj,
                       ( cls == OMM_MSG_REFRESH && me.subType == (byte) SNAP
                         ? RaiMsgEvent::SNAP : RaiMsgEvent::UPDATE ),
                       msgType, recStatus, ostate, rstate, me.state,
                       0, 0, 0, 0 );
    me.count++;
    if ( RaiApi::contentEntitle( &raiMsg ) == false ) {
      logDebug( LDEBUG, "failed content entitlement test" );
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
}

void
RaiSubscribe_omm::startListener( void )
{
  RaiSubParameter parm = (RaiSubParameter) this->subType;
  int flags = ( parm == SNAP   ? OMM_LISTEN_SNAPSHOT :
                parm == UPDATE ? ( OMM_LISTEN_STREAMING | OMM_LISTEN_NO_REFRESH )
                               : OMM_LISTEN_STREAMING );
  omm_status status = ommEvent_CreateListener( &this->listenId, this->q.ommQ,
                                               this->q.session.tport,
                                               this->subject, flags,
                                               RaiSubscribe_omm::onMsg, this );
  if ( status != OMM_OK )
    throw RaiApi_omm::getOmmErr( status );
}

void
RaiSubscribe_omm::startTimer( Rai_u32 timeoutMSecs )
{
  if ( timeoutMSecs != 0 && this->subType != (byte) UPDATE &&
       this->state != STATE_WILDCARD && ! this->hasTimer ) {
    RaiSubscribeTimer_omm *t = NEW RaiSubscribeTimer_omm( *this );
    ommEvent timerId;
    this->hasTimer = true;
    ommEvent_CreateTimer( &timerId, this->q.session.xQ,
                          RaiSubscribeTimer_omm::onTimer,
                          (double) timeoutMSecs / 1000.0, t );
  }
}

void
RaiSubscribe_omm::Start( const char *subject,  RaiSubParameter parm,
                         Rai_u32 timeoutMSecs )
{
  if ( subject == NULL )
    throw RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT );
  if ( this->InProgress() )
    this->Cancel();

  STRDUP( this->subject, subject );
  parm = (RaiSubParameter) ( (byte) parm & BOTH );
  this->subType = parm;
  this->state   = STATE_NO_MSG; /* no wildcards with OMM item streams */

  if ( RaiApi::canSubscribe( subject ) == false ) {
    logDebug( LDEBUG, "entitle check failed" );
    throw RaiApiErr::getErr( RaiApiErr::NO_PERMISSION );
  }
  this->q.apiLock->lock();
  try {
    this->q.addSubList( this );
    this->q.subClock++;
    this->startTimer( timeoutMSecs );
    this->startListener();
  } catch ( RaiException e ) {
    logError( LERROR, e, "%s subject \"%s\"",
              parm == BOTH ? "Subscribe" :
              ( parm == SNAP ? "Snapshot" : "Listen" ), subject );
    this->q.apiLock->unlock();
    throw e;
  }
  this->q.apiLock->unlock();
}

/* re-request: close and reopen the stream, which solicits a new refresh */
void
RaiSubscribe_omm::Refresh( Rai_u32 timeoutMSecs )
{
  RaiException e2 = NULL;

  this->q.apiLock->lock();
  if ( this->listenId != OMM_INVALID_ID ) {
    ommEvent_Destroy( this->listenId );
    this->listenId = OMM_INVALID_ID;
  }
  try {
    this->startTimer( timeoutMSecs );
    this->startListener();
  } catch ( RaiException e ) {
    logError( LERROR, e, "Refresh subject \"%s\"", this->subject );
    e2 = e;
  }
  this->q.apiLock->unlock();
  if ( e2 != NULL )
    throw e2;
}

void
RaiSubscribeTimer_omm::onTimer( ommEvent ev,  ommMsg,  void *cl )
{
  RaiSubscribeTimer_omm * me = (RaiSubscribeTimer_omm *) cl;
  RaiQueue_omm     * q;
  RaiSubscribe_omm * s = NULL;

  ommEvent_Destroy( ev );

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
RaiSubscribe_omm::Cancel( void )
{
  this->q.apiLock->lock();
  this->q.rmSubList( this );
  this->q.subClock++;
  if ( this->listenId != OMM_INVALID_ID ) {
    ommEvent_Destroy( this->listenId );
    this->listenId = OMM_INVALID_ID;
  }
  this->q.apiLock->unlock();
}

const char *
RaiSubscribe_omm::Subject( void )
{
  return this->subject;
}

bool
RaiSubscribe_omm::InProgress( void )
{
  return this->listenId != OMM_INVALID_ID;
}

/* ---- publish (v1: not supported) ------------------------------------------ */

void
RaiPublish_omm::Publish( const char *,  const void *,  Rai_u32,  TimeNSecs,
                         Rai_u32 )
{
  throw RaiApiErr::getErr( RaiApiErr::UNSUPPORTED_MTHD );
}

/* ---- errors --------------------------------------------------------------- */

RaiException
RaiApi_omm::getOmmErr( omm_status status )
{
  static const char mod[] = "RaiApi_omm";
  static ErrorRec e[ 32 ];
  static const unsigned int numErrs = sizeof( e ) / sizeof( e[ 0 ] ) - 1;

  if ( status >= 0 && (unsigned int) status < numErrs ) {
    if ( e[ status ].module == NULL ) {
      e[ status ].status = (unsigned int) status;
      e[ status ].reason = ommStatus_GetText( status );
      e[ status ].module = mod;
    }
    return &e[ status ];
  }
  if ( e[ numErrs ].module == NULL ) {
    e[ numErrs ].status = 999;
    e[ numErrs ].reason = "OMM api call failed";
    e[ numErrs ].module = mod;
  }
  return &e[ numErrs ];
}
