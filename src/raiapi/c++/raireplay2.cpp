/* Copyright (c) 2009 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_test__raireplay2_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#if ! defined( _WIN32 ) && ! defined( _WIN64 )
#include <signal.h>
#else
#include <windows.h>
#endif 

#include "raiapi2.h"
#include "base/thread.h"
#include "stream/io_stream.h"
#include "base/file.h"

namespace {

struct RaiReplay2Args {
  rai::StringArg fileName_arg;
  rai::DoubleArg perSec_arg;
  rai::ULLongArg msgCount_arg;
  rai::BoolArg   publishOnce_arg;
  rai::StringArg prefix_arg;
  rai::BoolArg   rate_arg;
  rai::BoolArg   quiet_arg;
  rai::DoubleArg realtime_arg;
  rai::BoolArg   addNaE_arg;
  
  RaiReplay2Args() :
    fileName_arg(    "fileName", NULL, "<file> [<file> ...]",
                     "Replay file name(s)" ),
    perSec_arg(      "perSec", 1.0, "<num>", "Number of msgs per sec" ),
    msgCount_arg(    "msgCount", 0, "<num>",
                     "Number of msgs to publish, 0 for infinite" ),
    publishOnce_arg( "once", false, NULL,
                     "Don't rewind files, publish records only one time" ),
    prefix_arg(      "prefix", NULL, "<subject>", 
                     "Publish subject prefix" ),
    rate_arg(        "rate", false, NULL,
                     "Display publish rate info" ),
    quiet_arg(       "quiet", false, NULL,
                     "If false, display when starting to replay at the "
                     "beginning of the file" ),
    realtime_arg(    "realtime", 0.0, "<speed>",
                     "Replay messages at the speed that they "
                     "were recorded (0 = use -perSec, "
                     "1 = 1x record speed, 2.5 = 2.5x record speed)" ),
    addNaE_arg(      "addNaE", false, NULL,
                     "Add .NaE to subjects with less than three '.'" ) {}

  void getArgs( rai::Args &args ) const {
    args.add( &fileName_arg, rai::COMMAND_ARG | rai::RESOURCE_ARG |
                              rai::LIST_ARG );
    args.add( &perSec_arg );
    args.add( &msgCount_arg );
    args.add( &publishOnce_arg );
    args.add( &prefix_arg );
    args.add( &rate_arg );
    args.add( &quiet_arg );
    args.add( &realtime_arg );
    args.add( &addNaE_arg );
  }
};

struct RaiReplay2 : public RaiTimerCallback {
  RaiApi           * api;
  RaiSession       * session;
  RaiQueue         * pubQueue;
  RaiPublish       * pub;
  RaiTimer         * pubTimer,
                   * printTimer,
                   * startTimer;
  ullong             msgCount,
                     msgsClocked,
                     msgsSent,
                     bytesSent,
                     msgsPrint,
                     ivalBytesSent;
  double             msgsPerSec;
  unsigned int       fileNum,
                     fileCount,
                     errCount;
  rai::File        * fin;
  rai::FileOffset    fsz,
                     foff;
  void             * fptr;
  rai::TimeHires     startTime,
                     currentTime;
  rai::TimeMSecs     timer;
  char            ** files;
  byte             * msgBuf,
                   * msgPtr;
  char               subjectBuf[ rai::SassConst::MAX_SUBJECT_LEN ];
  unsigned int       msgTypeId,
                     prefixLen,
                     prefixDot,
                     msgSize;
  rai::TimeMSecs     ivalMSecs;
  rai::TimeHires     intervalStart,
                     baseTime;
  double             realtimeSpeed,
                     msgDelta,
                     msgDeltaBase;
  bool               publishOnce,
                     quit,
                     printRate,
                     printFile,
                     addNaE;

  SYS_OPS( RaiReplay2 );
  RaiReplay2()
    : api( 0 ), session( 0 ), pubQueue( 0 ), pub( 0 ), pubTimer( 0 ),
      printTimer( 0 ), startTimer( 0 ), msgCount( 0 ),
      msgsClocked( 0 ), msgsSent( 0 ), bytesSent( 0 ), msgsPrint( 0 ),
      ivalBytesSent( 0 ), msgsPerSec( 0 ), fileNum( 0 ), fileCount( 0 ),
      errCount( 0 ), fin( 0 ), fsz( 0 ), fptr( 0 ),
      startTime( 0 ), currentTime( 0 ), timer( 0 ), files( 0 ), msgBuf( 0 ),
      msgPtr( 0 ), msgTypeId( 0 ), prefixLen( 0 ), prefixDot( 0 ), msgSize( 0 ),
      ivalMSecs( 500 ), intervalStart( 0 ), baseTime( 0 ), realtimeSpeed( 0 ),
      msgDelta( 0 ), msgDeltaBase( 0 ), publishOnce( false ), quit( false ),
      printRate( false ), printFile( true ), addNaE( false ) {
    ::memset( this->subjectBuf, 0, sizeof( this->subjectBuf ) );
  }

  ~RaiReplay2() {
    this->closeInput();
    if ( this->pubTimer != NULL )
      delete this->pubTimer;
    if ( this->printTimer != NULL )
      delete this->printTimer;
    if ( this->startTimer != NULL )
      delete this->startTimer;
    if ( this->pub != NULL )
      delete this->pub;
    if ( this->pubQueue != NULL )
      delete this->pubQueue;
    if ( this->session != NULL )
      delete this->session;
    if ( this->files != NULL ) {
      for ( unsigned int i = 0; i < this->fileCount; i++ ) {
	if ( this->files[ i ] != NULL )
	  FREE( this->files[ i ] );
      }
      FREE( this->files );
    }
    if ( this->api != NULL )
      delete this->api;
  }

  void close( void ) {
    this->quit = true;
    if ( this->pubTimer != NULL )
      this->pubTimer->Stop();
    if ( this->printTimer != NULL )
      this->printTimer->Stop();
    if ( this->startTimer != NULL )
      this->startTimer->Stop();
    if ( this->pub != NULL )
      this->pub->Destroy();
    if ( this->pubQueue != NULL )
      this->pubQueue->Destroy();
    if ( this->session != NULL )
      this->session->Destroy();
    if ( this->api != NULL )
      this->api->Close();
  }

  static const char * prefix_null_check( const char *value ) {
    if ( value != NULL &&
         ( value[ 0 ] == '\0' || ::strcmp( value, "\"\"" ) == 0  ||
           ::strcmp( value, "-" ) == 0 ) )
      value = NULL;
    return value;
  }

  bool init( RaiApi *apip,  rai::Args &args ) {
    this->api = apip;
    try {
      unsigned int   i,
                     timeout;
      RaiReplay2Args repargs;

      this->fileCount = args.getNumValues( repargs.fileName_arg.name );
      if ( this->fileCount == 0 ) {
	static const rai::ErrorRec e = { 0, "No file, -fileName required",
					 "init"};
	throw &e;
      }

      RaiApi::OpenLog( args );
      this->api->ParseArgs( args );

      /* alloc space for the file names */
      MALLOC( sizeof( this->files[ 0 ] ) * this->fileCount, &this->files );
      /* STRDUP() requires files[ i ] == 0 or malloced space, it reallocs */
      ::memset( this->files, 0, sizeof( this->files[ 0 ] ) * this->fileCount );
      for ( i = 0; i < this->fileCount; i++ )
	STRDUP( this->files[ i ], args.getString( repargs.fileName_arg.name,i));
      this->api->PrintLog( LMINOR, "Found %u files to replay", this->fileCount);

      /* subject prefix */
      const char *prefix = prefix_null_check( args.getString( repargs.prefix_arg.name ) );
      if ( prefix == NULL )
        prefix = "";
      ::strncpy( this->subjectBuf, prefix, sizeof( this->subjectBuf ) - 1 );
      this->prefixLen = ::strlen( this->subjectBuf );
      this->prefixDot = 0;
      for ( unsigned int i = 0; i < this->prefixLen; i++ )
        if ( this->subjectBuf[ i ] == '.' )
          this->prefixDot++;

      /* number of messages to publish */
      this->msgCount   = args.getULLong( repargs.msgCount_arg.name );
      /* how fast to publish them */
      this->msgsPerSec = args.getDouble( repargs.perSec_arg.name );
      this->realtimeSpeed = args.getDouble( repargs.realtime_arg.name );
      this->addNaE = args.getBoolean( repargs.addNaE_arg.name );

      if ( this->msgsPerSec >= 100 || this->realtimeSpeed != 0.0 ) {
        timeout = 1;
        if ( this->realtimeSpeed != 0.0 ) {
          double cpms;
          /* normalize per cycle */
          rai::Time::getHiresTime( &cpms );
          this->realtimeSpeed = (double) cpms * 1000.0 / this->realtimeSpeed;
        }
      }
      else {
        timeout = (unsigned int) ( 100.0 / this->msgsPerSec );
      }
      /* if only publish each file one time */
      this->publishOnce = args.getBoolean( repargs.publishOnce_arg.name );
      /* print rate of publish to stdout */
      this->printRate   = args.getBoolean( repargs.rate_arg.name );
      this->printFile   = ! args.getBoolean( repargs.quiet_arg.name );
      this->openInput();

      this->session  = this->api->CreateSession();
      this->session->Start();
      this->pubQueue = this->session->CreateQueue( true );
      this->pub      = this->session->CreatePublish();

      this->startTimer = this->pubQueue->CreateTimer( this );
      this->startTimer->SetInterval( 1000 ); /* delay start 1 second */
      this->startTimer->Start();

      /* publish messages on a timer */
      this->pubTimer = this->pubQueue->CreateTimer( this );
      this->pubTimer->SetInterval( timeout );
      //this->pubTimer->Start(); /* delayed 1 second */

      /* print rate every second */
      if ( this->printRate ) {
        this->printTimer = this->pubQueue->CreateTimer( this );
        this->printTimer->SetInterval( 1000 );
        //this->printTimer->Start();
      }
      return true;
    } catch ( RaiException e ) {
      this->api->PrintLog( LERROR, e, "Not initialized, stopped" );
      return false;
    }
  }

  void serviceRun( void ) {
    this->dispatchLoop();
  }

  void dispatchLoop( void ) {
    while ( ! this->quit ) {
      try {
        this->pubQueue->TimedDispatch( 100 );
      } catch ( RaiException e ) {
        this->api->PrintLog( LERROR, e, "pubQueue dispatch" );
      }
    }
  }

  rai::TimeHires updateClock( void ) {
    rai::TimeHires t;
    double         cpms;

    t = rai::Time::getHiresTime( &cpms );
    if ( this->startTime == 0 ) {
      this->startTime   = t;
      this->msgsClocked = 0;
    }
    else if ( t > this->startTime ) {
      this->msgsClocked = (ullong) ( (double) ( t -
                                      this->startTime ) / (double) cpms *
                            (double) this->msgsPerSec / (double) 1000 + 0.5 );
    }
    return t;
  }

  bool readMsg( void ) {
    unsigned int len,
                 size,
                 dot;
    char         c;

  get_next_line:;
    dot = 0;
    for ( len = this->prefixLen; this->foff < this->fsz; ) {
      this->subjectBuf[ len ] = ((char *) this->fptr)[ this->foff++ ];
      if ( this->subjectBuf[ len ] == '\n' ) {
        this->subjectBuf[ len ] = '\0';
        break;
      }
      else if ( this->subjectBuf[ len ] == '.' ) {
        dot++;
      }
      if ( len < sizeof( this->subjectBuf ) - 1 )
        len++;
    }
    if ( len == this->prefixLen ||
         this->subjectBuf[ this->prefixLen ] == '#' ) {
      if ( this->foff == this->fsz )
        return 0;
      goto get_next_line;
    }
    if ( this->addNaE ) {
      if ( this->prefixDot + dot < 3 && len + 5 < sizeof( this->subjectBuf ) )
        ::memcpy( &this->subjectBuf[ len ], ".NaE", 5 );
    }
    size = 0;
    if ( this->realtimeSpeed == 0 ) {
      while ( this->foff < this->fsz ) {
        c = ((char *) this->fptr)[ this->foff++ ];
        if ( c < '0' || c > '9' ) {
          if ( c != '\n' ) {
            while ( this->foff < this->fsz &&
                    ((char *) this->fptr)[ this->foff++ ] != '\n' )
              ;
          }
          break;
        }
        size = ( size * 10 ) + ( c - '0' );
      }
    }
    else {
      bool haveSize = false, haveDelta = false, haveDot = false;
      double fraction = 10.0, delta = 0.0;
      while ( this->foff < this->fsz ) {
        c = ((char *) this->fptr)[ this->foff++ ];
        if ( c < '0' || c > '9' ) {
          if  ( c == '\n' )
            break;
          if ( c == ' ' ) {
            if ( ! haveSize )
              haveSize = true;
            else if ( ! haveDelta )
              haveDelta = true;
          }
          else if ( c == '.' )
            haveDot = true;
        }
        else if ( ! haveSize ) {
          size = ( size * 10 ) + ( c - '0' );
        }
        else if ( ! haveDelta ) {
          if ( ! haveDot )
            delta = ( delta * 10 ) + (double) ( c - '0' );
          else {
            delta += (double) ( c - '0' ) / fraction;
            fraction *= 10;
          }
        }
      }
      this->msgDelta = delta;
      if ( this->msgDelta > 1400000000.0 ) {
        if ( this->msgDeltaBase == 0 || this->msgDeltaBase > this->msgDelta )
          this->msgDeltaBase = this->msgDelta;
        this->msgDelta -= this->msgDeltaBase;
      }
    }
    if ( size == 0 ) {
      if ( this->foff == this->fsz )
        return false;
      goto get_next_line;
    }

    if ( this->foff + (rai::FileOffset) size > this->fsz ) {
      static const rai::ErrorRec e = { 1, "Truncated message", "replay" };
      this->api->PrintLog( LERROR, &e, "File %s offset %lu\n",
                     this->files[ this->fileNum ], (unsigned long) this->foff );
      this->foff = this->fsz;
      return 0;
    }
    this->msgBuf  = &((byte *) this->fptr)[ this->foff ];
    this->msgSize = size;
    this->foff   += (rai::FileOffset) size;
    return true;
  }

  virtual void onTimer( RaiTimer &timer,  void */* cl */ ) {
    if ( &timer == this->pubTimer )
      this->doPub();
    else if ( &timer == this->printTimer )
      this->doPrint();
    else if ( &timer == this->startTimer ) {
      this->startTimer->Stop();
      this->pubTimer->Start();
      if ( this->printTimer != NULL )
        this->printTimer->Start();
    }
  }

  void doPub( void ) {
    try {
      /* replay at realtime rate */
      if ( this->realtimeSpeed != 0.0 ) {
        for (;;) {
          if ( this->quit )
            return;
          if ( this->msgSize == 0 ) {
            if ( ! this->readMsg() )
              this->rotateInput();
          }
          if ( this->msgSize != 0 ) {
            if ( this->msgDelta == 0 ) /* if no delta in replay file */
              break;
            /* compare stamps */
            this->currentTime = rai::Time::getHiresTime();
            if ( ( (double) ( this->currentTime - this->baseTime ) /
                  this->realtimeSpeed ) <= this->msgDelta )
              return;
            this->publishMsg();
          }
        }
      }
      /* calculate how many messages should be sent at -perSec rate */
      this->currentTime = this->updateClock();

      /* replay at constant perSec rate */
      while ( this->msgsSent < this->msgsClocked && ! this->quit ) {
        if ( this->msgSize == 0 ) {
          if ( ! this->readMsg() )
            this->rotateInput();
        }
        if ( this->msgSize != 0 ) {
          this->publishMsg();
          /* update timestamp on occasion */
          if ( ( this->msgsSent & 0xff ) == 0 )
            this->currentTime = rai::Time::getHiresTime();
        }
      }
    } catch ( RaiException e ) {
      this->api->PrintLog( LERROR, e, "Publishing msg" );
      if ( this->errCount++ > 300 ) {
        this->pubTimer->Stop();
        this->quit = true;
        return;
      }
    }
  }

  void publishMsg( void ) {
    this->pub->Publish( this->subjectBuf, this->msgBuf, this->msgSize );
    this->msgsSent++;
    this->bytesSent += this->msgSize;
    this->msgSize    = 0;

    if ( this->msgCount != 0 && --this->msgCount == 0 ) {
      this->pubTimer->Stop();
      this->quit = true;
    }
  }

  void rotateInput( void ) {
    if ( this->fileCount == 1 && ! this->publishOnce ) {
      this->foff = 0;
      this->baseTime = rai::Time::getHiresTime();
      if ( ! this->printRate ) {
        if ( this->printFile )
          this->api->PrintLog( LMINOR, "File: %s", this->files[ 0 ] );
      }
    }
    else {
      this->closeInput();

      if ( ++this->fileNum >= this->fileCount ) {
        this->fileNum = 0;
        if ( this->publishOnce ) {
          this->pubTimer->Stop();
          this->quit = true;
          return;
        }
      }
      this->openInput();
    }
  }

  void openInput( void ) {
    this->fin  = rai::File::openFile(
                  this->files[ this->fileNum ], rai::File::FILE_RDONLY );
    this->fsz  = this->fin->length();
    this->fptr = this->fin->map( this->fsz, rai::File::FILE_RDONLY,
                                 1024 * 1024 );
    this->foff = 0;
    this->baseTime = rai::Time::getHiresTime();

    if ( ! this->printRate )
      if ( this->printFile )
        this->api->PrintLog( LMINOR, "File: %s", this->files[ this->fileNum ] );
  }

  void closeInput( void ) {
    if ( this->fptr != NULL ) {
      this->fin->unmap( this->fptr, this->fsz, 1024 * 1024 );
      this->fptr = NULL;
    }
    if ( this->fin != NULL ) {
      this->fin->close();
      delete this->fin;
      this->fin = NULL;
      this->fsz = 0;
      this->foff = 0;
    }
  }

  void doPrint( void ) {
    rai::TimeHires curTime;
    double       interval,
                 rate;
    ullong       bs;
    double       cpms;

    curTime  = rai::Time::getHiresTime( &cpms );
    interval = (double) ( curTime - this->intervalStart ) /
               (double) cpms / 1000.0;
    if ( interval >= 0.100 ) {
      this->intervalStart = curTime;

      rate = (double) ( this->msgsSent - this->msgsPrint );
      this->msgsPrint = this->msgsSent;
      rate /= interval;
      bs = this->bytesSent;

      const char * suffix = "";
      unsigned int digits = 0;
      if ( rate >= 950.0 ) {
        rate /= 1000.0;
        suffix = "k";
      }

      digits = ( rate >= 10000.0 ? 0 : ( rate >= 1000.0 ? 1 : 2 ) );
      rai::Sys::out->printf( "msgs=%.*f%s/s data=%.1fmbit/s\n",
                    digits, rate, suffix, 
                    (double) ( bs - this->ivalBytesSent ) /
                      1000.0 / 1000.0 * 8.0 / interval );
      rai::Sys::out->flush();

      this->ivalBytesSent = bs;
    }
  }
};
} // namespace

#ifdef RAI_DLL_EMBEDDED
/* this version can be loaded into raicache */

#include "raiapi2_service.h"

/* dll entry point, determined by service file name: rai_service_raireplay2.so*/
extern "C" RAI_DLL_EXPORT void
RAIREPLAY2_ServiceInitialize( void )
{
  rai::ServiceFactory * fact = NEW
    T_RaiApiServiceFactory< RaiReplay2Args,
      T_RaiApiServiceProto< RaiReplay2 > >( "raireplay2" );
  rai::ServiceFactory::installService( fact );
}

#else
/* this is cmdline main for static or dynamic loading middlewares */
static RaiReplay2 *replay;

#if ! defined( _WIN32 ) && ! defined( _WIN64 )
extern "C" void myInterruptHandler( int );
#else
extern "C" BOOL myCtrlHandler( DWORD );
#endif

int
main( int argc, char *argv[] )
{
#if ! defined( _WIN32 ) && ! defined( _WIN64 )
  struct sigaction nsa;

  /* do this before threads are created so that they inherit the sigs */
  ::memset( &nsa, 0, sizeof( nsa ) );
  ::sigemptyset( &nsa.sa_mask );
  nsa.sa_handler = ::myInterruptHandler;
  ::sigaction( SIGHUP, &nsa, NULL );
  ::sigaction( SIGINT, &nsa, NULL );
  ::sigaction( SIGTERM, &nsa, NULL );
#else
  ::SetConsoleCtrlHandler( (PHANDLER_ROUTINE) ::myCtrlHandler, TRUE );
#endif

  rai::Args      args;
  RaiReplay2Args repargs;
  RaiApi       * api = NULL;

  rai::Sys::initialize();
  rai::Log::openLog( "-", rai::Log::LVL_MINOR, 4 );

  try {
    /* Open the api type from the command line, looks for -api <name> in
     * argc/argv[] and loads that middleware.  Program could also pass "tibrv"
     * or some other api name in the first argument.  If neither are specfied
     * then the default api is loaded (capr) */
    api = RaiApi::RaiOpen( NULL, argc, argv );
    /* get the api's configuration arguments */
    api->GetArgs( args );
    /* get the subject and settings for the program */
    repargs.getArgs( args );
    /* get the logging, version, help, rc arguments and sets error output */
    args.addDefaults( api->RaiVersion(), "rai_", rai::Sys::err, argv[ 0 ] );

    try {
      if ( args.processArgs( argc, argv ) ) {
        replay = NEW RaiReplay2();
        /* create api elements and start the dispatch thread */
        if ( replay->init( api, args ) )
          replay->dispatchLoop();
        /* stop publishers, if any, close the api */
        replay->close();
        delete replay;
      }
    } catch ( RaiException e ) {
      logError( LERROR, e, "Main" );
    }
  } catch ( RaiException e ) {
    logError( LERROR, e, "Unable to load Rai API" );
  }

  rai::Log::closeLog();
  rai::Sys::terminate();

  return 0;
}

#if ! defined( _WIN32 ) && ! defined( _WIN64 )
void
myInterruptHandler( int sig ) {
  if ( sig == SIGHUP )
    return;
  rai::Sys::err->printf( "Caught signal %d event, shutting down\n", sig );
  if ( replay == NULL )
    exit( 1 );
  replay->quit = true;
}
#else
BOOL
myCtrlHandler( DWORD fdwCtrlType )
{
  const char *s = NULL;
  switch ( fdwCtrlType ) {
    case CTRL_C_EVENT:      s = "ctrl-c"; break;
    case CTRL_CLOSE_EVENT:  s = "close"; break;
    case CTRL_BREAK_EVENT:  s = "ctrl-break"; break;
    case CTRL_LOGOFF_EVENT: s = "logoff"; break;
    case CTRL_SHUTDOWN_EVENT:
    default:                s = "shutdown"; break;
  }
  rai::Sys::err->printf( "Caught %s event, shutting down\n", s );
  if ( replay == NULL )
    exit( 1 );
  if ( replay != NULL )
    replay->quit = true;
  return TRUE;
}
#endif
#endif
