/* Copyright (c) 2009 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_api__raiapi2_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#if ! defined( RAIAPI_DLL_EXP ) && defined( RAI_DLL )
#define RAIAPI_DLL_EXP __declspec(dllexport) 
#endif

#include <string.h>
#include <ctype.h>

#include "raiapi2.h"
#include "stream/byte_array_stream.h"
#include "util/str_util.h"
#include "util/atomic.h"
#include "base/thread.h"
#include "base/sys.h"
#include "raimdapi_version.h"

using namespace rai;

static RaiEntitlement *entitlements = NULL;
extern "C" RaiApi *RaiApi_RaiOpen_tibrv( int argc,  char *argv[] );
extern "C" RaiApi *RaiApi_RaiOpen_omm( int argc,  char *argv[] );
const char DEFAULT_PROTO[] = "tibrv";

RaiApi *
RaiApi::RaiOpen( const char *name,  int argc,  char *argv[] )

{
  for ( int i = 1; i + 1 < argc; i++ ) {
    if ( ::strcmp( argv[ i ], "-api" ) == 0 ||
         ::strcmp( argv[ i ], "--api" ) == 0 )
      name = argv[ i + 1 ];
  }
  if ( name == NULL )
    name = DEFAULT_PROTO;
  /* statically link these */
  if ( ::strcmp( name, "tibrv" ) == 0 )
    return RaiApi_RaiOpen_tibrv( argc, argv );
  if ( ::strcmp( name, "omm" ) == 0 )
    return RaiApi_RaiOpen_omm( argc, argv );
  throw RaiApiErr::getErr( RaiApiErr::UNSUPPORTED_TSPT );
}


void
RaiApi::GetArgs( Args &args )
{
  static const StringArg api( raiapi_api_arg, DEFAULT_PROTO, "<name>",
                       "Api protocol name to use" );
  static const StringArg userid( raiapi_userid_arg, NULL, "<name>",
                       "User ID to login with NULL to disable" );
  static const StringArg appid( raiapi_appid_arg, "raiapi", "<name>",
                       "Application ID to use for identity" );
  args.add( &api );
  args.add( &userid );
  args.add( &appid );
}


void
RaiApi::GetDictArgs( Args &args )
{
  static const StringArg cfilePath(  cfile_path_arg, NULL, "<path>",
                       "Path to use when locating fields and records "
                       "elements should be separated by spaces" );
  static const StringArg tssFields(  tss_fields_arg, "tss_fields.cf", "<file>",
                       "Sass fields config file" );
  static const StringArg tssRecords( tss_records_arg, "tss_records.cf", "<file>",
                       "Sass records config file" );
  static const StringArg appendixA(  appendix_a_arg, "RDMFieldDictionary", "<file>",
                       "Marketfeed fields config file" );
  static const StringArg enumtypeDef( enumtype_def_arg, "enumtype.def", "<file>",
                       "Marketfeed enums config file" );
  args.add( &cfilePath );
  args.add( &tssFields );
  args.add( &tssRecords );
  args.add( &appendixA );
  args.add( &enumtypeDef );
}


void
RaiApi::ParseArgs( Args &args )
{
  if ( args.exists( raiapi_api_arg ) ) {
    const char *api  = args.getString( raiapi_api_arg );
    STRDUP( this->api, api );
  }
  if ( args.exists( raiapi_userid_arg ) ) {
    const char *user = args.getString( raiapi_userid_arg );
    STRDUP( this->userid, user );
  }
  if ( args.exists( raiapi_appid_arg ) ) {
    const char *app  = args.getString( raiapi_appid_arg );
    STRDUP( this->appid, app );
  }
}


bool
RaiApi::SetIoctl( const char *parameter,  const void *value )

{ 
  if ( ::strcmp( parameter, raiapi_api_arg ) == 0 )
    STRDUP( this->api, (const char *) value ); 
  else if ( ::strcmp( parameter, raiapi_userid_arg ) == 0 )
    STRDUP( this->userid, (const char *) value );
  else if ( ::strcmp( parameter, raiapi_appid_arg ) == 0 )
    STRDUP( this->appid, (const char *) value );
  else
    return false;
  return true;
}


bool
RaiApi::GetIoctl( const char *parameter,  void *value )
{ 
  if ( ::strcmp( parameter, raiapi_api_arg ) == 0 )
    *(char **) value = this->api;
  else if ( ::strcmp( parameter, raiapi_userid_arg ) == 0 )
    *(char **) value = this->userid;
  else if ( ::strcmp( parameter, raiapi_appid_arg ) == 0 )
    *(char **) value = this->appid;
  else
    return false;
  return true;
}


const char *
RaiApi::RaiVersion( void )
{
  /* "1.0.0-1 (f06c315a)", from the generated raimdapi_version.h */
  return RAIMDAPI_VER_STR;
}


bool
RaiApi::OpenLog( Args &args )
{
  const char * logName   = NULL,
             * levelName = NULL;
  unsigned int verbosity = Log::VERB_3;
  bool         useXml    = false;

  if ( args.exists( Args::log->name ) )
    logName = args.getString( Args::log->name );
  if ( args.exists( Args::logLevel->name ) )
    levelName = args.getString( Args::logLevel->name );
  if ( args.exists( Args::logVerb->name ) )
    verbosity = args.getUInt( Args::logVerb->name );
  if ( args.exists( Args::logXml->name ) )
    useXml = args.getBoolean( Args::logXml->name );

  if ( logName != NULL || levelName != NULL ) {
    Log::openLog( logName, Log::parseLogLevel( levelName ),
                  (Log::LogVerbosity) verbosity, useXml );
    return true;
  }
  return false;
}


void
RaiApi::PrintLog( Log::LogLevel level,  const char *where,  int line,
                  RaiException err,  const char *fmt,  ... )
{
  va_list ap;

  if ( Log::minLevel <= level ) {
    va_start( ap, fmt );
    Log::vprintLog( level, where, line, err, fmt, ap );
    va_end( ap );
  }
}


bool
RaiApi::OpenDict( Args &args )
{
  if ( args.exists( cfile_path_arg ) ) {
    const char * cfile_p  = args.getString( cfile_path_arg );
    if ( cfile_p != NULL &&
         args.exists( tss_records_arg ) &&
         args.exists( tss_fields_arg ) ) {
      const char * tss_rec  = args.getString( tss_records_arg ),
                 * tss_flds = args.getString( tss_fields_arg );
      RaiMsg_config * dict;

      dict = RaiMsg_config::parseDictionary( tss_flds, tss_rec, cfile_p );
      RaiMsg::SetDataDictionary( dict );

      if ( args.exists( appendix_a_arg ) &&
           args.exists( enumtype_def_arg ) ) {
        const char * app_a  = args.getString( appendix_a_arg ),
                   * enum_d = args.getString( enumtype_def_arg );
                       
        try {
          RaiMfeed_dict *mdict;
          mdict = RaiMfeed_dict::parseDictionary2( app_a, enum_d, cfile_p,
                                                   0, NULL, Log::LVL_MINOR );
          RaiMfeed_dict::SetMfeedDictionary( mdict );
          mdict->indexSass();
        } catch ( ... ) {
        }
      }
      return true;
    }
  }
  return false;
}

RaiSession::RaiSession() : sessionName( 0 )
{
  char objectSessionName[50];
  StrUtil::intToString( (ullong) (ulongptr) (void *) this,
                         objectSessionName, 50, U_HEX );
  STRDUP( sessionName, objectSessionName );
}

RaiSession::~RaiSession()
{
  FREE( sessionName );
}

void
RaiSession::Start( void )
{
}

void
RaiSession::setSessionName( const char* sessionName )
{
  FREE( this->sessionName );
  this->sessionName = 0;
  STRDUP( this->sessionName, sessionName );
}

const char *
RaiSession::getSessionName() const
{
  return sessionName;
}

void
RaiDataLossCallback::onConnection( RaiConnectionEvent &/* event */,  void */* cl */ )
{
}

#if 0
void
RaiDict::SetDict( RaiMsg_config *dict )
{
  RaiMsg_config *dict2 = RaiMsg::GetDataDictionary();
  RaiMsg::SetDataDictionary( dict );
  if ( dict2 != NULL )
    RaiMsg_config::release( dict2 );
}
#endif

RaiPublish::RaiPublish( bool autoIncrement )
{
  this->prefix    = NULL;
  this->prefixLen = 0;
  this->nextSeqno = 0;
  this->autoInc   = autoIncrement;
}

RaiPublish::~RaiPublish()
{
  if ( this->prefix != NULL )
    FREE( this->prefix );
}

static unsigned int
getRaiMsgTypeId( RaiMsg_protocol proto )
{
  switch ( proto ) {
    case TIB_SASS_PROTO:      return TIB_SASS_TYPE_ID;
    case RV_PROTO:            return RVMSG_TYPE_ID;
    case CI_SASS_PROTO:       return CI_SASS_TYPE_ID;
    case RAIMSG_PROTO:        return RAIMSG_TYPE_ID;
    case TIB_SASS_FORM_PROTO: return TIB_SASS_FORM_TYPE_ID;
    case CI_SASS_FORM_PROTO:  return CI_SASS_FORM_TYPE_ID;
    default:                  return 0;
  }
}

void
RaiPublish::Publish( const char *subject,  RaiMsg &raiMsg,
                     TimeNSecs stamp )
{
  if ( this->autoInc ) {
    raiMsg.Update( "SEQ_NO", this->nextSeqno++ );
  }
  this->Publish( subject, raiMsg.Packed(), raiMsg.PackSize(), stamp,
                 getRaiMsgTypeId( raiMsg.GetProtocol() ) );
}

void
RaiPublish::Publish( const char *subject,  RaiMsg* raiMsg,  TimeNSecs stamp )

{     
  if ( this->autoInc ) {
    raiMsg->Update( "SEQ_NO", this->nextSeqno++ );
  }
  this->Publish( subject, raiMsg->Packed(), raiMsg->PackSize(), stamp,
                 getRaiMsgTypeId( raiMsg->GetProtocol() ) );
  delete raiMsg;
}

static const char * prefix_null_check( const char *value ) {
  if ( value != NULL &&
       ( value[ 0 ] == '\0' || ::strcmp( value, "\"\"" ) == 0  ||
         ::strcmp( value, "-" ) == 0 ) )
    value = NULL;
  return value;
}

void
RaiPublish::SetPrefix( const char *prefix )
{
  prefix = prefix_null_check( prefix );

  STRDUP( this->prefix, prefix );
  if ( prefix != NULL )
    this->prefixLen = ::strlen( prefix );
  else
    this->prefixLen = 0;
}

const char *
RaiPublish::GetPrefix( void )
{
  return this->prefix;
}

Rai_u32
RaiPublish::GetSeqno( void )
{
  return this->nextSeqno;
}

void
RaiPublish::SetSeqno( Rai_u32 newSeqno )
{
  this->nextSeqno = newSeqno;
}


void
RaiApi::RaiLogin( RaiSession *session, const char *userDetails )
{
  if( strcmp(userDetails, "none" ) != 0 ){
// need to check to see if already logged in ? 
    if ( entitlements != NULL ){
      logError( LERROR, NULL, "Cannot log in to entitlements, already logged in" );
      return;
    }
    //entitlements = NEW RaiEntitleImpl();
    entitlements = session->Login( userDetails );

    if( entitlements == NULL ){
      logError( LERROR, NULL, "Cannot login to entitlements, no implementation" );
      return;
    }
    try{
      entitlements->Load( session, userDetails );
    }
    catch( RaiException e ) {
      logError( LERROR, e, "entitlement load error" );
    }
    if( entitlements->HaveEntitlements() != true ){
      entitlements->LoadLocal( userDetails );
//      entitlements->LazyLoad( session, userDetails );
    }
  }
  else {
    logMinor( LMINOR, "Entitlements explicitly disabled" );
  }
}

bool
RaiApi::haveEntitle( void )
{
  if ( entitlements != NULL )
    return entitlements->HaveEntitlements();
  return false;
}

bool
RaiApi::canSubscribe( const char *subject )
{
  if( entitlements != NULL )
    return entitlements->canSubscribe( subject );
  return true;
}

bool
RaiApi::canPublish( const char *subject )
{
  if( entitlements != NULL )
    return entitlements->canPublish( subject );
  return true;
}

bool
RaiApi::contentEntitle( RaiMsg *raiMsg )
{
  if( entitlements != NULL )
    return entitlements->contentEntitle( raiMsg );
  return true;
}

/******************************************************************************
 *
 * Static methods to initialize messages
 *
 *****************************************************************************/

static const char MSG_TYPE_F[]   = "MSG_TYPE",
                  REC_TYPE_F[]   = "REC_TYPE",
                  SEQ_NO_F[]     = "SEQ_NO",
                  REC_STATUS_F[] = "REC_STATUS";
RaiMsg *
RaiApi::NewRaiMsg( RaiMsg_protocol proto,  Rai_u16 MsgType,
                   const char *FormType,  Rai_u16 SeqNo,
                   Rai_u16 RecStatus )
{
  const RaiMsg_form *form;
  if ( FormType != NULL && FormType[ 0 ] == '\0' )
    FormType = NULL;
  if ( DataDictionary != NULL && FormType != NULL &&
       (form = DataDictionary->getForm( FormType )) != NULL )
    return RaiApi::NewRaiMsg( proto, MsgType, form->entry->fid, SeqNo,
                              RecStatus );
  RaiMsg * raiMsg = NEW RaiMsg( proto ); 

  raiMsg->Append( MSG_TYPE_F, MsgType );
  if ( FormType != NULL )
    raiMsg->Append( REC_TYPE_F, FormType );
  raiMsg->Append( SEQ_NO_F, SeqNo ); 
  raiMsg->Append( REC_STATUS_F, RecStatus );

  return raiMsg;
}


RaiMsg *
RaiApi::NewRaiMsg( RaiMsg_protocol proto,  Rai_u16 MsgType,  Rai_u16 RecType,
                   Rai_u16 SeqNo,  Rai_u16 RecStatus )
{
  RaiMsg * raiMsg = NEW RaiMsg( proto ); 

  if ( DataDictionary != NULL ) {
    raiMsg->Append( DataDictionary->msgType, MsgType );
    raiMsg->Append( DataDictionary->recType, RecType );
    raiMsg->Append( DataDictionary->seqNo, SeqNo ); 
    raiMsg->Append( DataDictionary->recStatus, RecStatus );
  }
  else {
    raiMsg->Append( MSG_TYPE_F, MsgType );
    raiMsg->Append( REC_TYPE_F, RecType );
    raiMsg->Append( SEQ_NO_F, SeqNo ); 
    raiMsg->Append( REC_STATUS_F, RecStatus );
  }

  return raiMsg;
}


const char *
RaiSubscribe::StateToString( RaiSubState state )
{
  static const char *state_str[] = {
    "NO_MSG", "WILDCARD", "NO_HDR", "INITIAL",
    "UPDATE", "NOTFOUND", "STALE", "DROPPED"
  };
  return state_str[ (unsigned) state & 0x7 ];
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
    /*  6 */ { BAD_SASS_INIT,     "Error creating or initializing a RaiMsg",
               mod },
    /*  7 */ { DICT_LOAD_PENDING, "Dictionary load pending", mod },
    /*  8 */ { ENT_LOGIN_PENDING, "Entitlement login pending", mod },
    /*  9 */ { BAD_ENT,           "Error waiting for entile login response",
               mod },
    /* 10 */ { NO_PERMISSION,     "User does not have permissions for subject",
               mod },
    /* 11 */ { BAD_TRANSPORT,     "Transport does not support this feature",
               mod },
    /* 12 */ { UNSUPPORTED_TSPT, "Unsuported transport type, linked wrong lib?",
               mod },
    /* 13 */ { UNSUPPORTED_MTHD,  "Method Unsuported in this transport", mod },
    /* 14 */ { TSPT_DATALOSS,     "Transport lost data or connection", mod },
    /* 15 */ { TSPT_RECV_ERR,     "Transport recv message error", mod },
    /* 16 */ { BAD_PUBLISH,       "Publisher invalid", mod },
    /* 17 */ { BAD_TIMER,         "Timer invalid", mod },
    /* 18 */ { BAD_SUBSCRIBE,     "Subscribe invalid", mod },
    /* 19 */ { BAD_MSG_TYPE,      "Message type name invalid", mod },
  };
  static const unsigned int numErrs = sizeof( err ) / sizeof( err[ 0 ] ) - 1;

  return &err[ status < numErrs ? status : numErrs ];
}


