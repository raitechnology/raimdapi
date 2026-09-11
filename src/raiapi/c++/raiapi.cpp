/* Copyright (c) 2004 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_raiapi__raiapi_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#include <string.h>
#include "raiapiP.h"
#include "base/sys.h"
#include "base/log.h"
#include "util/args.h"
#include "stream/io_stream.h"
#include "msg/rai_msg.h"
#include "util/str_util.h"
#include "raimdapi_version.h"

using namespace rai;

namespace rai_old {

/******************************************************************************
 *
 * Timer Interface Class
 *
 *****************************************************************************/

RaiTimerImpl::RaiTimerImpl( RaiTimer * /* timer */, RaiSession * session, RaiTimerCallback callback,
                            TimeMSecs interval, void * closure ) {
  tibrv_f64       rv_interval;
  tibrv_status    status;
  void          * cl;
  
  rv_interval = (tibrv_f64) interval / 1000.0; /* msecs -> secs, keep fraction */
  cl = this->AddTimer( session, callback, closure );
  status = tibrvEvent_CreateTimer(&this->TimerEvent, session->sessionImpl->rvQ,
                                  RaiTimerImpl::RV_callback, rv_interval, cl );
  if ( status != TIBRV_OK ) {
    logError( LERROR, badRvStatus( status ), "CreateTimer" );
    throw ( badRvStatus( status ));
  }
  logDebug( LDEBUG, "Created timer %d\n", this->TimerEvent );
}

TimerHandle *
RaiTimerImpl::AddTimer( RaiSession * session, RaiTimerCallback callback, void * closure )
{
  TimerHandle * handle;
  
  handle = NEW TimerHandle;
  handle->session = session;
  handle->callback = callback;
  handle->arg = closure;
  handle->Id = 24;
  
  return( handle );
}

// don't think I can use the rvId I for the session anymore here ---dna
void 
RaiTimerImpl::RV_callback( tibrvId /* I */, tibrvMsg /* msg */, void *cl){
  TimerHandle           * handle;
  RaiTimerCallback        callback;
  void                  * closure;
  
  try {
    logDebug( LDEBUG, "Got timer callback" );
    handle = (TimerHandle *) cl;
    callback = handle->callback;
    closure = handle->arg;
    
    callback( handle->session, closure );
  }
  catch ( RaiException e ) {
    logError( LERROR, e, "Timer callback forwarding failed" );
  }
}

RaiTimerImpl::~RaiTimerImpl() {
  tibrv_status status;
  
  status = tibrvEvent_Destroy(this->TimerEvent);
  if ( status != TIBRV_OK ) {
    logError( LERROR, badRvStatus( status ), "DestroyTimer" );
    //throw ( badRvStatus( status ));
  }
}

RaiTimer::RaiTimer( RaiSession * session, RaiTimerCallback callback,
                    TimeMSecs interval, void * closure ){  
  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }
  
  timerImpl = NEW RaiTimerImpl( this, session, callback, interval, closure );
}

TimeMSecs RaiTimer::GetInterval( void ){
  TimeMSecs interval;
  tibrv_f64 rv_interval;
  tibrv_status status;

  status = tibrvEvent_GetTimerInterval( this->timerImpl->TimerEvent, &rv_interval );
  if ( status != TIBRV_OK ) {
    logError( LERROR, badRvStatus( status ), "GetTimerInterval" );
    throw ( badRvStatus( status ));
  }
  // convert from a f64 to Millisecs
  interval = (TimeMSecs) (rv_interval * 1000);  // seconds into milliseconds 
  return( interval);
}

void RaiTimer::SetInterval( TimeMSecs interval ){
  tibrv_f64 rv_interval;
  tibrv_status status;
  
  rv_interval = (tibrv_f64) interval / 1000.0; /* msecs -> secs, keep fraction */
  status = tibrvEvent_ResetTimerInterval( this->timerImpl->TimerEvent, rv_interval );
  if ( status != TIBRV_OK ) {
    logError( LERROR, badRvStatus( status ), "SetTimerInterval" );
    throw ( badRvStatus( status ));
  }
}

RaiTimer::~RaiTimer(){
  if( timerImpl ) {
    delete timerImpl;
  }
}

/******************************************************************************
 *
 * Entitlement Interface Class
 *
 *****************************************************************************/

Mutex * RaiEntImpl::lock = NULL;

void
RaiEntImpl::EntAnnOnMsg( tibrvId /* I */, tibrvMsg /* msg */, void *cl) {
  RaiEntImpl    * impl = ( RaiEntImpl * ) cl;

  logDebug( LDEBUG, "Received callback for entitle announce " );
  if( impl->haveAnnounce == true )
    return;
  if( impl->userLogin != NULL ) {
    if( impl->userCredentials == NULL ) 
      impl->Login( impl->session, (char *)impl->userLogin );
  }
  else{  // use the default credentials
    logDebug( LDEBUG, "using default credentils" );
    impl->userCredentials = ".*";
#if ! defined( _WIN32 ) && ! defined( _WIN64 )
    regcomp( &impl->regexBuf, impl->userCredentials, REG_EXTENDED );
#endif
  }
  impl->haveAnnounce = true;
}

void 
RaiEntImpl::EntOnMsg( tibrvId /* I */, tibrvMsg msg, void *cl) {
  RaiMsg          raiMsg;
  tibrv_status    status;
  RaiField        field;
  RaiMsg_protocol proto;
  RaiMsg_size     off;
  const void    * msgp;
  tibrv_u32       msgSize;
  RaiEntImpl    * impl = ( RaiEntImpl * ) cl;
  const char    * entData;

  if( RaiApi::logLevel >= Trace )
    logDebug( LDEBUG, "Received callback for entitle login request" );

  tibrvMsg_GetByteSize( msg, &msgSize );
  status = tibrvMsg_GetAsBytes( msg, &msgp );
  if ( status != TIBRV_OK ) {
    logError( LERROR, badRvStatus( status ), "GetOpaque" );
  }
  if ( ! RaiMsg::ExtractProtocolEx( (RaiMsg_data) msgp, msgSize, proto,
                                    off ) )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_MAGIC_NUMBER );
  raiMsg.UnPack( proto, &((byte *) msgp)[ off ], msgSize - off,
                 RAIMSG_MEMORY_STATIC );
  raiMsg.Get("DATA", entData );
  raiMsg.Print(Sys::out);
  Sys::out->flush();

  impl->haveLoggedIn = true;
  impl->userCredentials = strdup( entData );
#if ! defined( _WIN32 ) && ! defined( _WIN64 )
  regcomp( &impl->regexBuf, entData, REG_EXTENDED );
#endif
}

void
RaiEntImpl::Login( RaiSession *session, char *userLogin )
{
  tibrv_status          status;

  try{
    logDebug( LDEBUG, "entitlement login" );
    this->userLogin = strdup( userLogin );
    this->session = session;
    status = tibrvQueue_Create( &this->rvQ2 );
      if( status != TIBRV_OK )
        throw badRvStatus( status );
    status = tibrvEvent_CreateListener( &this->rvD2, this->rvQ2,
                                        this->EntAnnOnMsg, session->sessionImpl->rvT,
                                        "RAIENT.ANNOUNCE", this );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
  }
  catch ( RaiException e ) {
    if( this->rvD2 )
      status = tibrvEvent_Destroy( this->rvD2 );
    if( this->rvQ2 )
      status = tibrvQueue_Destroy( this->rvQ2 );
    throw ( e );
        logError( LERROR, e, "Entitle Login failed" );
  }
}

void
RaiEntImpl::Load( RaiSession *session, char *loginDetails )
{
  if( ! lock->tryLock() ) {
    throw( RaiApiErr::getErr( RaiApiErr::ENT_LOGIN_PENDING ) );
  }
  
  try {
    RaiApi::entitleLoginInProgress = true;
    if( session->sessionImpl->transport == TCP ){
      TCPLoad( session, loginDetails );
    } else {
      RVLoad( session, loginDetails );
    }
  } catch ( RaiException e ) {
    RaiApi::entitleLoginInProgress = false;
    lock->unlock();
    throw( e );
  }

  RaiApi::entitleLoginInProgress = false;
  lock->unlock();
 
}

void 
RaiEntImpl::TCPLoad( RaiSession * /* session */, char * /* loginDetails */ ) {

};


void 
RaiEntImpl::RVLoad( RaiSession *session, char *loginDetails ) {
  tibrv_status          status;
  char                  entInBox[256];
  RaiMsg                raiMsg;
  tibrvMsg              m;
  char                  subject[32];
  
  ::strcpy(subject, "_TIC.RAIENT.LOGIN");
  try{
    if( RaiApi::logLevel >= Trace )
      logDebug( LDEBUG, "Requesting entitle login over RV on subject %s", subject );
    status = tibrvQueue_Create( &this->rvQ );
      if( status != TIBRV_OK )
        throw badRvStatus( status );

    status = tibrvTransport_CreateInbox( session->sessionImpl->rvT, entInBox, sizeof( entInBox ) );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    status = tibrvEvent_CreateListener( &this->rvD, this->rvQ,
                                        this->EntOnMsg, session->sessionImpl->rvT,
                                        entInBox, this );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    
    raiMsg.ReUse( RV_PROTO );
    raiMsg.Append("MSG_TYPE", "ENT_LOGIN" );
    raiMsg.Append("USER_NAME", loginDetails ); 
    raiMsg.Append("APP_NAME", "testapp" );
    raiMsg.Append("HOSTNAME", "testapp" );
    raiMsg.Append("IP_ADDRESS", "testapp" );
    raiMsg.Append("MAC_ADDRESS", "testapp" );
    raiMsg.Append("INBOX_ROOT", "testapp" );
    status = tibrvMsg_Create( &m );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    status = tibrvMsg_AddOpaque( m, "_data_", (void *) raiMsg.Packed(),
                               (tibrv_u32) raiMsg.PackSize() );
    status = tibrvMsg_SetSendSubject( m, subject);
    status = tibrvMsg_SetReplySubject( m, entInBox);
    status = tibrvTransport_Send( session->sessionImpl->rvT, m );
    if( RaiApi::logLevel >= Trace ){
      logDebug( LDEBUG, "RV entitle login request sent" );
    }
    //tibrvMsg_Destroy(m);
    WaitForEnt( session );
    if( this->rvD )
      status = tibrvEvent_Destroy( this->rvD );
    if( this->rvQ )
      status = tibrvQueue_Destroy( this->rvQ );
  }
  catch ( RaiException e ) {
    if( this->rvD )
      status = tibrvEvent_Destroy( this->rvD );
    if( this->rvQ )
      status = tibrvQueue_Destroy( this->rvQ );
    throw ( e );
    //    logError( LERROR, e, "Entitle Load failed" );
  }
}

void
RaiEntImpl::WaitForEnt(RaiSession *session){

  tibrv_status status;

  try {
    if( session->sessionImpl->transport == TCP ){
      //sleep( RaiApi::EntTimeOutSeconds * 1000 );
    } else {    //rv
      if( RaiApi::logLevel >= Trace ){
        logDebug( LDEBUG, "calling timed dispatch");
      }
      status = tibrvQueue_TimedDispatch( this->rvQ, RaiApi::DictTimeOutSeconds );
      if (( status != TIBRV_OK ) && ( status != TIBRV_TIMEOUT ))
        throw badRvStatus( status );
      }
    if( haveLoggedIn == false ){
      throw( RaiApiErr::getErr( RaiApiErr::BAD_ENT ) );
    }
  } catch ( RaiException e) {
    throw( e );
  }
}

bool
RaiEntImpl::match( const char *subject )
{
  int        result = 0;
#if ! defined( _WIN32 ) && ! defined( _WIN64 )
  size_t     nmatch;
  regmatch_t pmatch[10];

  nmatch = 8;
#endif
  if( this->userCredentials != NULL ){
    logDebug( LDEBUG, "subject %s cred ::%s::", subject, this->userCredentials );
#if ! defined( _WIN32 ) && ! defined( _WIN64 )
    result = regexec( &this->regexBuf, subject, nmatch, pmatch, 0 ); 
#endif
  }
  if( result == 0 )
    return true;
  logDebug( LDEBUG, "regexc returned %d", result );
  return false;
}

bool
RaiEntImpl::canSubscribe( const char *subject ){

  if( strcmp( subject, "TEST.NOSUB" ) == 0 )
    return false;
  return this->match( subject );
}

bool
RaiEntImpl::canPublish( const char *subject ){

  if( strcmp( subject, "TEST.NOPUB" ) == 0 )
    return false;
  return this->match( subject );
}


/******************************************************************************
 *
 * Dictionary Interface Class
 *
 *****************************************************************************/

RaiDict::RaiDict() {  
  RaiMsg_config  *  dict;

  this->haveDictionary = false;
  this->isDispatcher = true;

  this->dictImpl = NEW RaiDictImpl( this );

  try {
    dict = RaiMsg::GetDataDictionary();
    if ( dict != NULL) {
      RaiMsg_config::release( dict );
      RaiMsg::SetDataDictionary( NULL );
    }
  } catch ( RaiException e ) {
    logError( LERROR, e, "Dictionary creation failed");
  }
}      

Mutex * RaiDictImpl::lock = NULL;

void 
RaiDictImpl::DictOnMsg( tibrvId /* I */, tibrvMsg msg, void *cl) {
  RaiMsg          raiMsg;
  tibrv_u32       msgSize;
  const void    * bufp;
  tibrv_status    status;
  RaiMsg_config * dict;
  RaiDictImpl   * impl = ( RaiDictImpl * ) cl;

  // see if we can unpack the dictionary
  try {
    dict = RaiMsg::GetDataDictionary();
    if ( dict != NULL) {
      RaiMsg_config::release( dict );
      RaiMsg::SetDataDictionary( NULL );
    }
    status = tibrvMsg_GetOpaque(msg, "_data_", &bufp, &msgSize);
    if ( status != TIBRV_OK ) {
      logError( LERROR, badRvStatus( status ), "GetOpaque" );
    }
    else {
      try {
        raiMsg.UnPack( (char * ) bufp, msgSize );
        dict = RaiMsg_config::unpackDataDictionary( raiMsg );
        RaiMsg::SetDataDictionary( dict );
      } catch ( ... ) {
        return; // cope with rv bug where license warning comes on inbox
      }
      if( RaiApi::logLevel >= Debug ){
        logDebug( LDEBUG, "dictionary received"); 
      }
      impl->me->haveDictionary = true;
    }
  } catch (RaiException e) {
    logError( LERROR, e, "Dictionary unpack failed");
  }
}

void
RaiDictImpl::Load( RaiSession *session, char *dictSubject )
{
  if( ! lock->tryLock() ) {
    throw( RaiApiErr::getErr( RaiApiErr::DICT_LOAD_PENDING ) );
  }
  
  try {
    RaiApi::dictionaryLoadInProgress = true;
    this->me->haveDictionary = false;
    if( session->sessionImpl->transport == TCP ){
      TCPLoad( session, dictSubject );
    } else {
      RVLoad( session, dictSubject );
    }
  } catch ( RaiException e ) {
    RaiApi::haveDictionary = false;
    RaiApi::dictionaryLoadInProgress = false;
    lock->unlock();
    throw( e );
  }

  RaiApi::dictionaryLoadInProgress = false;
  lock->unlock();
 
}

void
RaiDict::Load( RaiSession *session, char *dictSubject ) {

  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }
  if( ! dictSubject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }

  dictImpl->Load( session, dictSubject );
}

void 
RaiDictImpl::TCPLoad( RaiSession */* session */, char */* dictSubject */ ) {

};

void 
RaiDictImpl::RVLoad( RaiSession *session, char *dictSubject ) {
  tibrv_status          status;
  char                  dictInBox[256];
  tibrvMsg              m;
  
  try{
    if( RaiApi::logLevel >= Trace )
      logDebug( LDEBUG, "Requesting dictionary over RV on subject %s", dictSubject );
    //if( session->theDispatcher != NULL ){
    //  session->theDispatcher.sleep( ( RaiApi::DictTimeOutSeconds + 1 ) * 1000); //sleep for one more second
    //} 
    status = tibrvQueue_Create( &this->rvQ );
      if( status != TIBRV_OK )
        throw badRvStatus( status );

    status = tibrvTransport_CreateInbox( session->sessionImpl->rvT, dictInBox, sizeof( dictInBox ) );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    status = tibrvEvent_CreateListener( &this->rvD, this->rvQ,
                                        this->DictOnMsg, session->sessionImpl->rvT,
                                        dictInBox, this );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    
    status = tibrvMsg_Create( &m );
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
    //::strcpy(dict, "_TIC.REPLY.SASS.DATA.DICTIONARY");
    status = tibrvMsg_SetSendSubject( m, dictSubject);
    status = tibrvMsg_SetReplySubject( m, dictInBox);
    status = tibrvTransport_Send( session->sessionImpl->rvT, m );
    if( RaiApi::logLevel >= Trace ){
      logDebug( LDEBUG, "RV dictionary request sent" );
    }
    //tibrvMsg_Destroy(m);
    WaitForDict( session );
    if( this->rvD )
      status = tibrvEvent_Destroy( this->rvD );
    if( this->rvQ )
      status = tibrvQueue_Destroy( this->rvQ );
  }
  catch ( RaiException e ) {
    if( this->rvD )
      status = tibrvEvent_Destroy( this->rvD );
    if( this->rvQ )
      status = tibrvQueue_Destroy( this->rvQ );
    throw ( e );
    //    logError( LERROR, e, "Dictionary Load failed" );
  }
}

void
RaiDictImpl::WaitForDict(RaiSession *session){

  tibrv_status status;

  try {
    if( session->sessionImpl->transport == TCP ){
      //sleep( RaiApi::DictTimeOutSeconds * 1000 );
    } else {    //rv
      if( RaiApi::logLevel >= Trace ){
        logDebug( LDEBUG, "calling timed dispatch");
      }
      status = tibrvQueue_TimedDispatch( this->rvQ, RaiApi::DictTimeOutSeconds );
      if (( status != TIBRV_OK ) && ( status != TIBRV_TIMEOUT ))
        throw badRvStatus( status );
      }
    //session.tcpDictionaryWait = false;
    if( this->me->haveDictionary == false ){
      //      RaiException e = RaiApiErr::getErr( RaiApiErr::BAD_DICT );
      //   logError( LERROR, e, "Timed out after %d seconds",
      //        RaiApi::DictTimeOutSeconds );
      throw( RaiApiErr::getErr( RaiApiErr::BAD_DICT ) );
    }
  } catch ( RaiException e) {
    //    logError( LERROR, e, "Exception caught waiting for dictionary" );
    throw( e );
  }
}

RaiDict::~RaiDict(){
  delete dictImpl;
};

/******************************************************************************
 *
 * Session Interface
 *
 *****************************************************************************/

RaiSessionImpl::RaiSessionImpl( RaiSession * session, const char *svcname, const char *netname, 
                                const char *dmnname )
{
  tibrv_status  status;
  this->me   = session;
  this->transport = RV7;
  this->rvQ  = 0;
  this->rvT  = 0;
  this->rvI  = 0;
  this->rvI2 = 0;
  this->rvDispatcher = 0;
  this->subscribedSubject = true;
  this->receivedSubject = true;

  status = tibrvQueue_Create( &this->rvQ );
  if ( status == TIBRV_OK )
    status = tibrvTransport_Create( &this->rvT, svcname, netname, dmnname );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchMode( this->rvT, TIBRV_TRANSPORT_SINGLE_BATCH );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchSize( this->rvT, 16384 );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchInterval( this->rvT, 0.1 );
  if ( status == TIBRV_OK )
    status = tibrvTransport_SetBatchDispatchFlush( this->rvT, TIBRV_TRUE );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
}

void
RaiSessionImpl::onMsg( tibrvId /* rvI */, tibrvMsg msg, void *cl )
{
  tibrv_status    status;
  RaiMsg          raiMsg;
  RaiField        field;
  RaiMsg_protocol proto;
  RaiMsg_size     off;
  const void    * msgp;
  tibrv_u32       msgSize;
  SubHandle     * handle;
  RaiCallback     onMsg;
  RaiEvent        event;
  const char    * recSubject;
  
  try {
    /* find the subscriber in the switch table */
    handle = (SubHandle *) cl;
    
    /* get the function to call */
    onMsg = handle->callback;
    
    /* get the original closure */
    cl = handle->arg;
    
    /* strip the outer part of the rvMsg */
    //status = tibrvMsg_GetOpaque( msg, "_data_", &msgp, &msgSize );
    tibrvMsg_GetByteSize( msg, &msgSize );
    status = tibrvMsg_GetAsBytes( msg, &msgp );
    if ( status != TIBRV_OK ) {
      logError( LERROR, badRvStatus( status ), "GetOpaque" );
    }
    
    if ( ! RaiMsg::ExtractProtocolEx( (RaiMsg_data) msgp, msgSize, proto,
                                      off ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_MAGIC_NUMBER );
    raiMsg.UnPack( proto, &((byte *) msgp)[ off ], msgSize - off,
                   RAIMSG_MEMORY_STATIC );
    //raiMsg->Print(Sys::out);
    //Sys::out->flush();
    
    event.session = handle->session;
    if( handle->session->sessionImpl->receivedSubject == true ){
      status = tibrvMsg_GetSendSubject( msg, &recSubject );
      if ( status != TIBRV_OK ) {
        logError( LERROR, badRvStatus( status ), "GetSubject" );
      }
      event.receivedSubject = recSubject;
    }
    if( handle->session->sessionImpl->subscribedSubject == true )
      event.subscribedSubject = handle->subSubject;

    /* call the registered function and pass the closure */
    onMsg( &event, &raiMsg, cl);
  }
  catch ( RaiException e ) {
    logError( LERROR, e, "Message unpack failed" );
  }
}

// don't set the session in the handle - and use it in the callback - check this --dna
// set explicitly after making call to AddSubscriber
SubHandle *
RaiSessionImpl::AddSubscriber( RaiCallback callback, void * closure )
{
  SubHandle * handle;

  handle = NEW SubHandle;
  handle->callback = callback;
  handle->arg = closure;

  return( handle );
}

RaiSessionImpl::~RaiSessionImpl()
{
  if ( this->rvI != 0 )
    tibrvEvent_Destroy( this->rvI );
  if ( this->rvI2 != 0 )
    tibrvEvent_Destroy( this->rvI2 );
  if ( this->rvT != 0 )
    tibrvTransport_Destroy( this->rvT );
  if ( this->rvQ != 0 )
    tibrvQueue_Destroy( this->rvQ );
}

RaiSession::RaiSession( const char *svcname, const char *netname, 
                        const char *dmnname )
{

  this->sessionImpl = NEW RaiSessionImpl( this, svcname, netname, dmnname );
}

RaiSession::RaiSession( const char *svcname, const char *netname, 
                        const char *dmnname, bool createDispatcher )
{
  tibrv_status status;

  this->sessionImpl = NEW RaiSessionImpl( this, svcname, netname, dmnname );
  if( createDispatcher ){
    status = tibrvDispatcher_Create( &this->sessionImpl->rvDispatcher, this->sessionImpl->rvQ );
    if( status != TIBRV_OK )
      throw badRvStatus( status );
  }
}

void
RaiSession::Destroy()
{
  if( sessionImpl ) {
    delete sessionImpl;
    sessionImpl = NULL;
  }

}

RaiSession::~RaiSession()
{
  Destroy();
}

RaiEvent::RaiEvent()
{
  this->receivedSubject = NULL;
  this->subscribedSubject = NULL;
  this->session = NULL;
}

RaiEvent::~RaiEvent()
{
}

/******************************************************************************
 *
 * Publisher Interface
 *
 *****************************************************************************/

RaiPublish::RaiPublish( RaiSession * session, bool isComplex )
{
  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }

  this->session = session;
  this->type = 0;
  this->form = 0;
  this->subject = NULL;
  this->typenam = NULL;
  this->formname = NULL;
  this->seqNo = 0;
  this->isComplex = isComplex;
};

RaiPublish::RaiPublish( RaiSession * session, const char *subject,
                        bool isComplex )
{
  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }

  if( ! subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }

  this->session = session;
  this->type = 0;
  this->form = 0;
  this->subject = NULL;
  this->typenam = NULL;
  this->formname = NULL;
  this->seqNo = 0;
  this->isComplex = isComplex;

  STRDUP( this->subject, subject );
};

void 
RaiPublish::Publish( RaiMsg * raiMsg )
{
  if( ! this->subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }

  Publish(this->subject, raiMsg);
};

void
RaiPublish::Publish(const char * subject, RaiMsg * raiMsg )
{
  tibrvMsg      m;
  tibrv_status  status;

  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }
  if( ! subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }
  if( ! raiMsg ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_RAIMSG ) );
  }

  if( RaiApi::entitleImpl.canPublish( subject ) == false ) {
    throw( RaiApiErr::getErr( RaiApiErr::NO_PERMISSION ) );
  }

  if( ! this->isComplex )
    raiMsg->Update( "SEQ_NO", (Rai_u16) this->seqNo++ );

#if 0
  char ticName[256];
  ::strcpy( ticName, "_TIC." );
  str_copy( &ticName[ 5 ], subject, sizeof( ticName ) - 5 );
#endif
  logDebug( LDEBUG, "Publishing to %s, msgSize %u", subject,
            raiMsg->PackSize() );
      
  // now wrap the message in a RVMsg and send it
  if ( raiMsg->GetProtocol() == RV_PROTO ) {
    status = tibrvMsg_CreateFromBytes( &m, (void *) raiMsg->Packed() );
  }
  else {
    status = tibrvMsg_Create( &m );
    if ( status == TIBRV_OK )
      status = tibrvMsg_AddOpaque( m, "_data_", (void *) raiMsg->Packed(),
                                   (tibrv_u32) raiMsg->PackSize() );
  }
  /*
    An alternative way to do this not using the rvMsg API directly
    RaiMsg msg;
    byte   msgBuf2[ 16 * 1024 ];
    msg.InitBuffer( (char *) msgBuf2, 0, sizeof( msgBuf2 ), RV_PROTO,
    RAIMSG_MEMORY_STATIC );
    msg.Append( "_data_", RAIMSG_OPAQUE, (RaiMsg_size) size,
    (RaiMsg_data) msgBuf );
    status = rv_Send( this->session, this->subjectBuf, RVMSG_RVMSG,
    (rvmsg_Size) msg.PackSize(),
    (rvmsg_Data) msg.Packed() );

  */
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  status = tibrvMsg_SetSendSubject( m, subject );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  status = tibrvTransport_Send( session->sessionImpl->rvT, m );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  tibrvMsg_Destroy( m );

};

void
RaiPublish::Publish( byte * buffer, unsigned int size )
{
  if( ! this->subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }

  Publish( this->subject, buffer, size );
};

void
RaiPublish::Publish( const char * subject, 
                     byte * buffer, unsigned int size )
{
  tibrvMsg      m;
  tibrv_status  status;
  
  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }
  if( ! subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }
  if( ! buffer ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_BUFFER ) );
  }
  
  logDebug( LDEBUG, "Publishing to %s, msgSize %u", subject, size );
      
  // now wrap the message in a RVMsg and send it
  status = tibrvMsg_Create( &m );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  status = tibrvMsg_AddOpaque( m, "_data_", (void *) buffer, size );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  status = tibrvMsg_SetSendSubject( m, subject );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  status = tibrvTransport_Send( session->sessionImpl->rvT, m );
  if ( status != TIBRV_OK )
    throw badRvStatus( status );
  tibrvMsg_Destroy( m );  
};

void
RaiPublish::Ioctl( IoctlParameter parameter, void *value)
{

  switch ( parameter ) {
    
  case SetPubType:
    // cast value to char * and test OK
    if ( StrUtil::strcasecmp( (char *)value, "VERIFY" ) == 0 )
      this->type = 0;
    else if ( StrUtil::strcasecmp( (char *)value, "UPDATE" ) == 0 )
      this->type = 1;
    else
      this->type = 8;
    break;
    
  case SetForm:
    try {
      StrUtil::parseInt( this->formname, &this->form );
      this->usesSass = true;
    } catch ( ... ) {
      this->form = 0;
      this->usesSass = false;
    }
    break;
    
  default:
    throw( RaiApiErr::getErr( RaiApiErr::BAD_IOCTL_PARAM ) );
    break;
  }
}; 

void
RaiPublish::Destroy(){

  this->session  = NULL;
  if ( this->subject != NULL ) {
    FREE( this->subject );
    this->subject = NULL;
  }
  if ( this->typenam != NULL ) {
    FREE( this->typenam );
    this->typenam = NULL;
  }
  if ( this->formname != NULL ) {
    FREE( this->formname );
    this->formname = NULL;
  }
};

RaiPublish::~RaiPublish(){
  this->Destroy();
};

/******************************************************************************
 *
 * General Interface
 *
 *****************************************************************************/

bool RaiApi::dictionaryLoadInProgress;
bool RaiApi::entitleLoginInProgress;
bool RaiApi::haveDictionary;
int  RaiApi::DictTimeOutSeconds;
int  RaiApi::logLevel;
RaiEntImpl RaiApi::entitleImpl;
static const unsigned int RAI_API_VERBOSITY =
 Log::VERB_SEVERITY | Log::VERB_NUMBER | Log::VERB_REASON | Log::VERB_DESCR;

static unsigned int openCount;
void
RaiApi::RaiOpen( RaiTransport /* transport */, RaiProto /* protocol */)
{
  tibrv_status  status;

  if ( openCount++ == 0 ) {
    Sys::initialize();
    Log::openLog( "-", Log::LVL_ERROR, RAI_API_VERBOSITY, false );
    dictionaryLoadInProgress = false;
    entitleLoginInProgress = false;
    haveDictionary = false;
    DictTimeOutSeconds = 10;
    logLevel = 0;

    status = tibrv_Open();
    if ( status != TIBRV_OK )
      throw badRvStatus( status );
  }
};

void
RaiApi::RaiLogin( RaiSession *session, char * userLogin )
{

    if( strcmp(userLogin, "none" ) != 0 )
      entitleImpl.Load( session, userLogin );
    // entitle.Login( session, userLogin ); 
    // this should do login after announce msg received
    // but not working correctly 
}

const char * 
RaiApi::RaiVersion( void )
{
  const char * version;

  version = "RaiApi Version " RAIMDAPI_VER_STR " C++"; /* raimdapi_version.h */
  return( version );
}

void
RaiApi::RaiClose( void )
{
  if ( openCount > 0 && --openCount == 0 ) {
    /*
     * loop through open sessions and close off the resources
     */
       
    tibrv_Close();
    Log::closeLog();
    Sys::terminate();
  }
};

void
RaiApi::RaiMainloop( RaiSession * session )
{
  tibrv_status  status;

  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }

  for (;;) {
    // status = tibrvQueue_Dispatch( this->rvQ );
    status = tibrvQueue_TimedDispatch( session->sessionImpl->rvQ, 1 );
    if (( status != TIBRV_OK ) && ( status != TIBRV_TIMEOUT ))
      throw badRvStatus( status );
  }
};

// how to diferentiate between a successful dispatch and a time out??
void
RaiApi::RaiTimedDispatch(RaiSession *session, unsigned int interval )
{
  tibrv_status status;

  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }

  status = tibrvQueue_TimedDispatch( session->sessionImpl->rvQ, interval );
  if (( status != TIBRV_OK ) && ( status != TIBRV_TIMEOUT ))
    throw badRvStatus( status );

};

void
RaiApi::SetDictTimeoutSec( int sec )
{
  if( sec > 0 )
    RaiApi::DictTimeOutSeconds = sec;
}

int
RaiApi::GetDictTimeoutSec( void )
{
  return( RaiApi::DictTimeOutSeconds );
}

bool
RaiApi::GetHaveDict()
{
  return( RaiApi::haveDictionary );
}

bool
RaiApi::GetDictLoadInProgress()
{
  return( RaiApi::dictionaryLoadInProgress );
}

void
RaiApi::SetLogLevel( int level )
{
  switch ( level ) {
    case 0:
    case Major:
      Log::setLevel( Log::LVL_ERROR, false, RAI_API_VERBOSITY );
      break;
    case Minor:
      Log::setLevel( Log::LVL_MINOR, false, RAI_API_VERBOSITY );
      break;
    case Debug:
    case Trace:
    case Full:
      Log::setLevel( Log::LVL_DEBUG, false, RAI_API_VERBOSITY );
      break;
  }
  if( (level >= 0 ) && ( level <= 5 ) )
    RaiApi::logLevel = level;
}

int
RaiApi::GetLogLevel( void )
{
  return( RaiApi::logLevel );
}

/******************************************************************************
 *
 * Static methods to initialize messages
 *
 *****************************************************************************/

  RaiMsg *
  RaiApi::NewSASSMsg( Sass::MsgType MsgType, short RecType,
                      short SeqNo, Sass::RecStatus RecStatus )
  {
    RaiMsg * raiMsg = NULL;

    try {
      raiMsg = NEW RaiMsg( TIB_SASS_PROTO );
      raiMsg->Append( "MSG_TYPE",(short) MsgType );
      raiMsg->Append( "REC_TYPE", RecType );       
      raiMsg->Append( "SEQ_NO", SeqNo ); 
      raiMsg->Append( "REC_STATUS", (short) RecStatus );
    }
    catch ( RaiException e ) {
      throw( RaiApiErr::getErr( RaiApiErr::BAD_SASS_INIT ) );
    } 
    return raiMsg;
  };

  RaiMsg *
  RaiApi::NewSASSMsg( Sass::MsgType MsgType, const char * FormType,
                      short SeqNo, Sass::RecStatus RecStatus )

  { 
    RaiMsg * raiMsg = NULL;

    try {
      raiMsg = NEW RaiMsg( TIB_SASS_PROTO ); 
      raiMsg->Append( "MSG_TYPE",(short) MsgType );
      if ( FormType != NULL ) {
        const RaiMsg_form * form;

        if( DataDictionary != NULL &&
            (form = DataDictionary->getForm( FormType )) != NULL )
          raiMsg->Append( "REC_TYPE", form->entry->fid );
        else 
          raiMsg->Append( "REC_TYPE",  FormType );       
      }
      else 
        raiMsg->Append( "REC_TYPE", 0 );
      raiMsg->Append( "SEQ_NO", SeqNo ); 
      raiMsg->Append( "REC_STATUS", (short) RecStatus );
    } 
    catch (RaiException e) {
      throw( RaiApiErr::getErr( RaiApiErr::BAD_SASS_INIT ) );
    }
    return raiMsg;
  };
  
  RaiMsg *
  RaiApi::NewRaiMsg( Sass::MsgType MsgType, short RecType,
                     short SeqNo, Sass::RecStatus RecStatus )

  {
    RaiMsg * raiMsg = NULL;

    try {
      raiMsg = NEW RaiMsg( RAIMSG_PROTO );
      raiMsg->Append( "MSG_TYPE",(short) MsgType );
      raiMsg->Append( "REC_TYPE", RecType );       
      raiMsg->Append( "SEQ_NO", SeqNo ); 
      raiMsg->Append( "REC_STATUS", (short) RecStatus );
    }
    catch (RaiException e) {
      throw( RaiApiErr::getErr( RaiApiErr::BAD_SASS_INIT ) );
    }
    return raiMsg;
  };

  RaiMsg *
  RaiApi::NewRaiMsg( Sass::MsgType MsgType, const char * RecType,
                     short SeqNo, Sass::RecStatus RecStatus )

  {
    RaiMsg * raiMsg = NULL;

    try {
      raiMsg = NEW RaiMsg( RAIMSG_PROTO ); 
      raiMsg->Append( "MSG_TYPE",(short) MsgType );
      if ( RecType != NULL ) {
        const RaiMsg_form * form;

        if( DataDictionary != NULL &&
            (form = DataDictionary->getForm( RecType )) != NULL )
          raiMsg->Append( "REC_TYPE", form->entry->fid );
        else 
          raiMsg->Append( "REC_TYPE",  RecType );       
      }
      else 
        raiMsg->Append( "REC_TYPE", 0 );
      raiMsg->Append( "SEQ_NO", SeqNo ); 
      raiMsg->Append( "REC_STATUS", (short) RecStatus );
    } 
    catch (RaiException e) {
      throw( RaiApiErr::getErr( RaiApiErr::BAD_SASS_INIT ) );
    }
    return raiMsg;
  };

/******************************************************************************
 *
 * Subscriber Interface
 *
 *****************************************************************************/

// need to take a property here to determine if we do the initial snap
RaiSubscribe::RaiSubscribe( RaiSession * session, const char * subject,
                            RaiCallback  callback, void * closure,
                            SubParameter parm)
{
  tibrv_status status;
  char inboxName[256];
  char initialSubjectName[256];
  tibrvMsg m;
  SubHandle * localHandle;

  if( ! session ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SESSION ) );
  }
  if( ! subject ) {
    throw( RaiApiErr::getErr( RaiApiErr::BAD_SUBJECT ) );
  }
  if( RaiApi::entitleImpl.canSubscribe( subject ) == false ) {
    throw( RaiApiErr::getErr( RaiApiErr::NO_PERMISSION ) );
  }
  

  /* create a handle in the session. save as a void * in the RaiSubscribe 
   * object so we can retrieve it later for destroying the listeners. This
   * keeps the structure of the handle internal (not exposed in the API
   */

  localHandle = session->sessionImpl->AddSubscriber(callback, closure);
  localHandle->session = session;
  STRDUP( localHandle->subSubject, subject );
  this->handle = (void *) localHandle; 

  // check the protocol and transport for the right handles
 
  try {
    if( parm != UPDATE ) {
      status = tibrvTransport_CreateInbox( session->sessionImpl->rvT, inboxName,
                                           sizeof( inboxName ) );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      
      status = tibrvEvent_CreateListener( &localHandle->subId, session->sessionImpl->rvQ,
                                          session->sessionImpl->onMsg, session->sessionImpl->rvT,
                                          inboxName, localHandle );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      status = tibrvMsg_Create( &m );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      ::strcpy( initialSubjectName, "_SNAP." );
      str_copy( &initialSubjectName[ 6 ], subject,
                sizeof( initialSubjectName ) - 6 );
      status = tibrvMsg_SetSendSubject( m, initialSubjectName );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      status = tibrvMsg_SetReplySubject( m, inboxName );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      if( parm == BOTH ){
        status = tibrvMsg_AddU16( m, "flags", 6 );
        if ( status != TIBRV_OK )
          throw badRvStatus( status );
      }
      status = tibrvTransport_Send( session->sessionImpl->rvT, m );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
      tibrvMsg_Destroy( m );
    } /* all cases except UPDATE */
    
    if( parm != SNAP ) {
      status = tibrvEvent_CreateListener( &localHandle->subId2, session->sessionImpl->rvQ,
                                          session->sessionImpl->onMsg, session->sessionImpl->rvT,
                                          subject, localHandle );
      if ( status != TIBRV_OK )
        throw badRvStatus( status );
    } /* all cases except SNAP */

  } catch ( RaiException e ){
    logError( LERROR, e, "Publish on subject: \"%s\"", subject );
  }
};
    
void
RaiSubscribe::Cancel()
{
  SubHandle * localHandle;

  localHandle = (SubHandle *) this->handle;
  if ( localHandle ) {
    if ( localHandle->subId != TIBRV_INVALID_ID ) {
      tibrvEvent_Destroy( localHandle->subId );
      localHandle->subId = TIBRV_INVALID_ID;
    }
    if ( localHandle->subId2 != TIBRV_INVALID_ID ) {
      tibrvEvent_Destroy( localHandle->subId2 );
      localHandle->subId2 = TIBRV_INVALID_ID;
    }
  }
}

RaiSubscribe::~RaiSubscribe()
{

  SubHandle * localHandle;

  localHandle = (SubHandle *) this->handle;
  if( localHandle ){
    delete localHandle;
  } 
};

/******************************************************************************
 *
 * RaiException Error Interface
 *
 *****************************************************************************/

RaiException
badRvStatus( tibrv_status status )
{
  static const char mod[] = "RaiApi";
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

RaiException
RaiApiErr::getErr( unsigned int status )
{
  static const char     mod[] = "RaiApi";
  static const ErrorRec err[] = {
    /*  0 */ { BAD_SESSION,       "NULL or invalid session parameter", mod },
    /*  1 */ { BAD_SUBJECT,       "NULL or invalid subject", mod },
    /*  2 */ { BAD_RAIMSG,        "NULL or invalid RaiMsg", mod },
    /*  3 */ { BAD_BUFFER,        "NULL buffer", mod },
    /*  4 */ { BAD_IOCTL_PARAM,   "Bad IOCTL Parameter", mod },
    /*  5 */ { BAD_DICT,          "Error waiting for dictionary to load", mod },
    /*  6 */ { BAD_SASS_INIT,     "Error creating or initializing a RaiMsg", mod },
    /*  7 */ { DICT_LOAD_PENDING, "Dictionary load pending", mod },
    /*  8 */ { ENT_LOGIN_PENDING, "Entitlement login pending", mod },
    /*  9 */ { BAD_ENT,           "Error waiting for entile login response", mod },
    /* 10 */ { NO_PERMISSION,     "User does not have permissions for subject", mod },
  };
  static const unsigned int numErrs = sizeof( err ) / sizeof( err[ 0 ] ) - 1;

  return &err[ status < numErrs ? status : numErrs ];
}

} /* namespace rai_old */
