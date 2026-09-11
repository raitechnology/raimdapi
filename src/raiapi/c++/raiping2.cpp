/* Copyright (c) 2009 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_api__raiping2_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#if ! defined( _WIN32 ) && ! defined( _WIN64 )
#include <signal.h>
#else
#include <windows.h>
#endif

#include "raiapi2.h"
#include "base/thread.h"
#include "stream/io_stream.h"
#include "util/str_util.h"

namespace {

struct RaiPing2Args {
  rai::DoubleArg perSec_arg;
  rai::ULLongArg msgCount_arg;
  rai::StringArg prefix_arg;
  rai::StringArg subject_arg;
  rai::BoolArg   direct_arg;
  rai::BoolArg   noSub_arg;
  rai::BoolArg   noPub_arg;
  rai::BoolArg   verify_arg;
  rai::BoolArg   initial_arg;
  rai::BoolArg   wallClockTime_arg;
  rai::StringArg timeSource_arg;
  char availDescr[ 256 ], defaultSrc[ 32 ];

  RaiPing2Args() :
    perSec_arg(   "perSec", 10.0, "<num>", "Number of msgs per sec" ),
    msgCount_arg( "msgCount", 0, "<num>", "Number of msgs to publish, "
         "0 for infinite" ),
    prefix_arg(  "prefix", NULL, "<subject>",
        "Subject to prefix publish subject with, usually set to "
        "_TIC. if using SASS/RV" ),
    subject_arg(  "subject", "PING.REC.TEST.NaE", "<subject>", 
         "Subject to ping" ),
    direct_arg(   "direct", false, "<bool>", 
         "Whether to dispatch messages directly from the recv "
         "threads (true) or serialized on the queue thread "
         "(false)" ),
    noSub_arg(   "noSub", false, "<bool>", 
         "Don't subscribe, only publish pings" ),
    noPub_arg(   "noPub", false, "<bool>", 
         "Don't publish, only subscribe pings" ),
    verify_arg( "verify", false, "<bool>",
         "Send VERIFY ping as first record so that it caches, "
         "this is necessary for ping to work with TIC" ),
    initial_arg( "initial", false, "<bool>",
         "Send INITIAL ping record as first record" ),
    wallClockTime_arg( "wallClockTime", false, "<bool>",
         "Use wall clock time instead of monotonic time which is only "
         "useful on the same machine that published the message" ),
    timeSource_arg( "ts", defaultSrc, "<source>", availDescr ) {}

  void getArgs( rai::Args &args,
                bool cmdLine = false ) {
    args.add( &perSec_arg );
    args.add( &msgCount_arg );
    args.add( &prefix_arg );
    args.add( &subject_arg );
    args.add( &direct_arg );
    args.add( &noSub_arg );
    args.add( &noPub_arg );
    args.add( &verify_arg );
    args.add( &initial_arg );
    args.add( &wallClockTime_arg );
    if ( cmdLine ) {
      /* this are only available to standalone */
      rai::Time::getAvailableTimeSources( availDescr, sizeof( availDescr ),
                                          defaultSrc, sizeof( defaultSrc ),
                                          false );
      args.add( &timeSource_arg );
    }
  }
};


class RaiPing2 : public RaiTimerCallback, public RaiMsgCallback,
                 public RaiDataLossCallback {
	SYS_OPS( RaiPing2 );
 public:
  RaiApi            * api;
  RaiSession        * session;
  RaiQueue          * subQueue,
                    * pubQueue;
  RaiSubscribe      * subscriber;
  RaiPublish        * publisher;
  RaiTimer          * publishTimer,
                    * printTimer;
  const RaiMsg_dict * msgTypeField,
                    * recTypeField,
                    * seqNoField,
                    * recStatusField,
                    * timeField;
  char              * subject,
                    * prefix,
                    * publishSubject;
  double              msgsPerSec;
  Rai_u64             msgsPublished,
                      msgsClocked,
                      msgCount,
                      msgSent,
                      msgRecvd,
                      lastMsgRecvd;
  rai::TimeHires      intervalStart,
                      startTime;
  double              latencySum,
                      latencyMin,
                      latencyMax,
                      cumLatencySum,
                      cumLatencyMin,
                      cumLatencyMax;
  Rai_u16             recType,
                      msgType;
  bool                direct,
                      quit,
                      noSub,
                      noPub,
                      wallClockTime;
  static const unsigned int MAX_LAT = 16000; /* 16ms */
  #define MIN_INIT_LAT 99999.99
  Rai_u64          msgsLat[ MAX_LAT ]; /* 1us -> 16ms */

  RaiPing2() : api( 0 ), session( 0 ), subQueue( 0 ),
      pubQueue( 0 ), subscriber( 0 ), publisher( 0 ), publishTimer( 0 ),
      printTimer( 0 ), msgTypeField( 0 ), recTypeField( 0 ), seqNoField( 0 ),
      recStatusField( 0 ), timeField( 0 ), subject( 0 ), prefix( 0 ),
      publishSubject( 0 ), msgsPerSec( 10.0 ), msgsPublished( 0 ),
      msgsClocked( 0 ), msgCount( 0 ), msgSent( 0 ), msgRecvd( 0 ),
      lastMsgRecvd( 0 ), intervalStart( 0 ), startTime( 0 ), latencySum( 0 ),
      latencyMin( MIN_INIT_LAT ), latencyMax( 0 ), cumLatencySum( 0 ),
      cumLatencyMin( MIN_INIT_LAT ), cumLatencyMax( 0 ), recType( 0 ),
      msgType( rai::SassConst::UPDATE ), direct( false ), quit( false ),
      noSub( false ), noPub( false ), wallClockTime( false ),
      pubThread( *this ) {
    ::memset( this->msgsLat, 0, sizeof( this->msgsLat ) );
  }

  ~RaiPing2() {
    if ( this->publishTimer != NULL )
      delete this->publishTimer;
    if ( this->printTimer != NULL )
      delete this->printTimer;
    if ( this->publisher != NULL )
      delete this->publisher;
    if ( this->subscriber != NULL )
      delete this->subscriber;
    if ( this->subQueue != NULL )
      delete this->subQueue;
    if ( this->pubQueue != NULL )
      delete this->pubQueue;
    if ( this->session != NULL )
      delete this->session;
    if ( this->publishSubject != NULL )
      FREE( this->publishSubject );
    if ( this->subject != NULL )
      FREE( this->subject );
    if ( this->prefix != NULL )
      FREE( this->prefix );
  }

  void close( void ) {
    this->final();
    this->quit = true;
    this->pubThread.signalDone();
    if ( ! this->pubThread.isThreadJoined() )
      this->pubThread.join();
    if ( this->publishTimer != NULL )
      this->publishTimer->Stop();
    if ( this->printTimer != NULL )
      this->printTimer->Stop();
    if ( this->publisher != NULL )
      this->publisher->Destroy();
    if ( this->subscriber != NULL )
      this->subscriber->Cancel();
    if ( this->subQueue != NULL )
      this->subQueue->Destroy();
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
      RaiPing2Args pargs;
      rai::TimeMSecs timeout;

      if ( args.exists( pargs.timeSource_arg.name ) &&
           args.isSet( pargs.timeSource_arg.name ) )
        rai::Time::initialize( args.getString( pargs.timeSource_arg.name ) );

      RaiApi::OpenLog( args );
      RaiApi::OpenDict( args );
      this->api->ParseArgs( args );

      STRDUP( this->subject, args.getString( pargs.subject_arg.name ) );
      STRDUP( this->prefix, prefix_null_check( args.getString( pargs.prefix_arg.name ) ) );
      this->msgCount      = args.getULLong( pargs.msgCount_arg.name );
      this->msgsPerSec    = args.getDouble( pargs.perSec_arg.name );
      this->direct        = args.getBoolean( pargs.direct_arg.name );
      this->noSub         = args.getBoolean( pargs.noSub_arg.name );
      this->noPub         = args.getBoolean( pargs.noPub_arg.name );
      this->wallClockTime = args.getBoolean( pargs.wallClockTime_arg.name );
      if ( args.getBoolean( pargs.verify_arg.name ) )
        this->msgType = rai::SassConst::VERIFY;
      if ( args.getBoolean( pargs.initial_arg.name ) )
        this->msgType = rai::SassConst::INITIAL;

      if ( this->prefix == NULL )
        STRDUP( this->publishSubject, this->subject );
      else {
        char buf[ rai::SassConst::MAX_SUBJECT_LEN ];
        rai::str_copy( buf, this->prefix, sizeof( buf ) );
        rai::str_cat( buf, this->subject, sizeof( buf ) );
        STRDUP( this->publishSubject, buf );
      }
      if ( ! this->noPub && ! this->noSub )
        this->api->PrintLog( LMINOR, "Publishing %s subscribe %s", 
                             this->publishSubject, this->subject );
      else if ( this->noPub )
        this->api->PrintLog( LMINOR, "Subscribe %s", this->subject );
      else if ( this->noSub )
        this->api->PrintLog( LMINOR, "Publishing %s", this->publishSubject );
      this->session    = this->api->CreateSession();
      this->session->SetDataLossCB( this );
      this->session->Start();
      this->subQueue   = this->session->CreateQueue( this->direct );
      this->pubQueue   = this->session->CreateQueue( this->direct );

      if ( rai::DataDictionary != NULL ) {
        const RaiMsg_form *form;
        if ( (form = rai::DataDictionary->getForm( "RAIPING" )) != NULL ) {
          this->recType = form->entry->fid;
          if ( (this->msgTypeField   = form->msgType) != NULL &&
               (this->recTypeField   = form->recType) != NULL &&
               (this->seqNoField     = form->seqNo) != NULL &&
               (this->recStatusField = form->recStatus) != NULL ) {
             this->timeField = form->getEntry( "time" );
             if ( this->timeField == NULL )
               this->timeField = form->getEntry( "TIME" );
             if ( this->timeField != NULL )
               this->api->PrintLog( LMINOR, "Using TIB_SASS form RAIPING" );
          }
        }
      }
      if ( ! this->noSub ) {
        this->subscriber = this->subQueue->CreateSubscribe( this );
        this->subscriber->Start( this->subject, RaiSubscribe::UPDATE );
      }
      if ( ! this->noPub ) {
        this->publisher = this->session->CreatePublish();
        /* timer is 10 times faster than rate since pub rate is controlled by
         * updateClock(), not by this timer */
        if ( this->msgsPerSec >= 100.0 )
          timeout = 1;
        else
          timeout = (rai::TimeMSecs)
            ( this->msgsPerSec == 0 ? 100.0 : 100.0 / this->msgsPerSec );

        this->publishTimer = this->pubQueue->CreateTimer( this );
        this->publishTimer->SetInterval( timeout );
        this->publishTimer->Start();
      }
      /* this->noPub == true */
      else {
        /* since updateClock() is never called, and onMsg() tests if messages
         * are published before calculating latency */
        this->startTime = 1;
      }
      if ( this->msgsPerSec > 15.0 || this->noPub ) {
        this->printTimer = this->pubQueue->CreateTimer( this );
        this->printTimer->SetInterval( 500 ); /* ms */
        this->intervalStart = rai::Time::getHiresTime();
        this->printTimer->Start();
      }
      return true;
    } catch ( RaiException e ) {
      this->api->PrintLog( LERROR, e, "Not initialized, stopped" );
      return false;
    }
  }

  /* RaiDataLossCallback */
  virtual void onConnection( RaiConnectionEvent &event,  void * /* cl */ ) {
    this->api->PrintLog( LMINOR, "%s", event.description );
  }
  virtual void onDataLoss( RaiDataLossEvent &event,  void * /* cl */ ) {
    RaiException e = RaiApiErr::getErr( RaiApiErr::TSPT_DATALOSS );
    this->api->PrintLog( LERROR, e, "%s", event.description );
  }

  rai::TimeHires updateClock( void ) {
    rai::TimeHires currentTime;
    double         cpms;

    currentTime = rai::Time::getHiresTime( &cpms );
    if ( this->startTime == 0 ) {
      this->startTime   = currentTime;
      this->msgsClocked = 0;
    }
    else if ( currentTime > this->startTime ) {
      this->msgsClocked = (Rai_u64)
        ( (double) ( currentTime - this->startTime ) / (double) cpms *
          (double) this->msgsPerSec / 1000.0 );
    }
    return currentTime;
  }

  /* RaiMsgCallback */
  virtual void onMsg( RaiMsgEvent &event,  RaiMsg &raiMsg,  void * /* closure */ ) {
    double         latencyMS,
                   cpms;
    rai::TimeHires sendTime,
                   curTime;
    ullong         rcvTime;
    bool           foundTime;

    if ( this->timeField != NULL )
      foundTime = raiMsg.Get( this->timeField, sendTime );
    else
      foundTime = raiMsg.Get( "time", sendTime );
    if ( foundTime ) {
      if ( sendTime < this->startTime || this->startTime == 0 ) /* old ping */
        return;
      curTime = rai::Time::getHiresTime( &cpms );
      if ( this->wallClockTime ) {
        rcvTime = rai::Time::hiresToNanosecs( curTime );
        cpms = 1000000.0;
      }
      else {
        rcvTime = curTime;
      }
      latencyMS = (double) ( rcvTime - sendTime ) / (double) cpms;
      this->latencySum += latencyMS;
      this->cumLatencySum += latencyMS;
      unsigned int usecsIndex = (unsigned int) ( latencyMS * 1000.0 );
      if ( usecsIndex >= MAX_LAT )
        usecsIndex = MAX_LAT - 1;
      this->msgsLat[ usecsIndex ]++;

      if ( latencyMS > this->latencyMax ) {
        this->latencyMax = latencyMS;
        if ( latencyMS > this->cumLatencyMax )
          this->cumLatencyMax = latencyMS;
      }
      if ( latencyMS < this->latencyMin ) {
        this->latencyMin = latencyMS;
        if ( latencyMS < this->cumLatencyMin )
          this->cumLatencyMin = latencyMS;
      }
      this->msgRecvd++;

      if ( this->printTimer == NULL ) {
        rai::Sys::out->printf( "%s cnt=%u latency=%.3f\n",
                      event.subject, (unsigned int) this->msgRecvd, latencyMS );
        rai::Sys::out->flush();
      }
      if ( this->msgRecvd == this->msgCount )
        this->quit = true;
    }
  }

  /* RaiTimerCallback */
  virtual void onTimer( RaiTimer &timer, void * /* closure */ ) {
    if ( &timer == this->publishTimer )
      this->publish();
    else if ( &timer == this->printTimer )
      this->print();
  }

  void publish( void ) {
    rai::TimeHires curTime;
    ullong pubTime;
    byte   msgBuf[ 1024 ];
    RaiMsg msg;

    try {
      curTime = this->updateClock();
      pubTime = ( this->wallClockTime ?
                  rai::Time::hiresToNanosecs( curTime ) : curTime );
      msgBuf[ 0 ] = 0;
      for ( ; this->msgsPublished < this->msgsClocked;
            this->msgsPublished++ ) {
        if ( this->timeField != NULL ) {
          if ( msgBuf[ 0 ] == 0 ) {
            msg.InitBuffer( msgBuf, 0, sizeof( msgBuf ), TIB_SASS_PROTO,
                            RAIMSG_MEMORY_STATIC );
            msg.Append( this->msgTypeField, this->msgType );
            msg.Append( this->recTypeField, (Rai_u16) this->recType );
            msg.Append( this->seqNoField, (Rai_u16) this->msgSent );
            msg.Append( this->recStatusField, (Rai_u16) 0 );
            msg.Append( this->timeField, pubTime );
          }
          else {
            if ( this->msgType != rai::SassConst::UPDATE ) {
              this->msgType = rai::SassConst::UPDATE;
              msg.Update( this->msgTypeField, rai::SassConst::UPDATE );
            }
            msg.Update( this->seqNoField, (Rai_u16) this->msgSent );
            msg.Update( this->timeField, pubTime );
          }
        }
        else {
          if ( msgBuf[ 0 ] == 0 ) {
            msg.InitBuffer( msgBuf, 0, sizeof( msgBuf ), RV_PROTO,
                            RAIMSG_MEMORY_STATIC );
            msg.Append( "MSG_TYPE", this->msgType );
            msg.Append( "SEQ_NO", (Rai_u16) this->msgSent );
            msg.Append( "REC_STATUS", (Rai_u16) 0 );
            msg.Append( "time", pubTime );
          }
          else {
            if ( this->msgType != rai::SassConst::UPDATE ) {
              this->msgType = rai::SassConst::UPDATE;
              msg.Update( "MSG_TYPE", rai::SassConst::UPDATE );
            }
            msg.Update( "SEQ_NO", (Rai_u16) this->msgSent );
            msg.Update( "time", pubTime );
          }
        }

        this->publisher->Publish( this->publishSubject, msg );
        if ( ++this->msgSent == this->msgCount ) {
          this->publishTimer->Stop();
          return;
        }
        curTime = rai::Time::getHiresTime();
        pubTime = ( this->wallClockTime ?
                    rai::Time::hiresToNanosecs( curTime ) : curTime );
      }
      if ( this->msgSent > 0 && this->msgType != rai::SassConst::UPDATE )
        this->msgType = rai::SassConst::UPDATE;
    } catch ( RaiException e ) {
      this->api->PrintLog( LERROR, e, "publish" );
    }
  }

  void print( void ) {
    rai::TimeHires curTime;
    double       ms,
                 interval,
                 rate,
                 min,
                 max,
                 cpms;

    curTime  = rai::Time::getHiresTime( &cpms );
    interval = (double) ( curTime - this->intervalStart ) /
               (double) cpms / 1000.0;
    if ( interval >= 0.100 ) {
      if ( this->msgRecvd > this->lastMsgRecvd ) {
        this->intervalStart = curTime; 

        rate = (double) ( this->msgRecvd - this->lastMsgRecvd );
        this->lastMsgRecvd = this->msgRecvd;
        ms    = this->latencySum / rate;
        rate /= interval;
        min   = this->latencyMin;
        max   = this->latencyMax;
        this->latencySum = 0;
        this->latencyMin = MIN_INIT_LAT;
        this->latencyMax = 0;

        if ( rate < 30.0 ) {
          if ( rate <= 1.1 ) {
            rai::Sys::out->printf( "%s: %.03f ms\n", this->subject, ms );
          }
          else {
            rai::Sys::out->printf( "%s: rate=%.01f/s av=%.03f min=%.03f"
                          " max=%.03fms\n", this->subject, rate, ms, min, max );
          }
        }
        else {
          const char *suffix = "";
          unsigned int digits = 0;
          if ( rate >= 950.0 ) {
            rate /= 1000.0;
            suffix = "k";
          }
          digits = ( rate >= 10000.0 ? 0 : ( rate >= 1000.0 ? 1 : 2 ) );
          rai::Sys::out->printf(
                            "%s: r=%.*f%s/s av=%.03f min=%.03f max=%.03fms\n",
                            this->subject, digits, rate, suffix, ms, min, max );
        }
        rai::Sys::out->flush();
      }
    }
  }

  void final( void ) {
    double       av;
    unsigned int avg, j, stdDev[ 4 ];
    Rai_u64      cnt,
                 stdDevMsgs[ 4 ];
    if ( ! this->noSub ) {
      if ( this->msgRecvd == 0 )
        av = 0;
      else
        av = this->cumLatencySum / (double) this->msgRecvd;
      avg = (unsigned int) ( av * 1000.0 );
      if ( avg >= MAX_LAT )
        avg = MAX_LAT - 1;
      cnt             = this->msgsLat[ avg ];
      stdDev[ 0 ]     = 1;
      stdDevMsgs[ 0 ] = 0;
      stdDevMsgs[ 1 ] = (Rai_u64) ( this->msgRecvd * 0.682 );
      stdDevMsgs[ 2 ] = (Rai_u64) ( this->msgRecvd * 0.955 );
      stdDevMsgs[ 3 ] = (Rai_u64) ( this->msgRecvd * 0.997 );

      for ( j = 1; j < 4; j++ ) {
        for ( stdDev[ j ] = stdDev[ j - 1 ];
              stdDev[ j ] < MAX_LAT && cnt < stdDevMsgs[ j ]; stdDev[ j ]++ ) {
          if ( stdDev[ j ] <= avg )
            cnt += this->msgsLat[ avg - stdDev[ j ] ];
          if ( avg + stdDev[ j ] < MAX_LAT )
            cnt += this->msgsLat[ avg + stdDev[ j ] ];
        }
      }
      rai::Sys::out->printf(
                "%s: av=%.03f min=%.03f max=%.03fms stddev=(%.03f,"
                "%.03f,%.03f) (68.2%%,95.5%%,99.7%%)\n",
                this->subject, av, this->cumLatencyMin, this->cumLatencyMax,
                (double) stdDev[ 1 ] / 1000.0, (double) stdDev[ 2 ] / 1000.0,
                (double) stdDev[ 3 ] / 1000.0 );
      if ( av > (double) MAX_LAT / 1000.0 ) {
        rai::Sys::out->printf( "stddev not accurate\n" );
      }
      rai::Sys::out->flush();
    }

    rai::Thread::CpuListT<64> cpu;
    unsigned int i;

    cpu.getRusage();

    if ( cpu.userTime + cpu.sysTime == 0 ) {
      rai::Sys::out->printf( "Pid=%u runtime=%.3f (no cpu time)\n",
              rai::Thread::getProcessId(), (double) cpu.totalTime / 1000.0 );
    }
    else {
      rai::Sys::out->printf( "Pid=%u runtime=%.3f  usr=%.3f + sys=%.3f = %.3f"
                        " total cpu time used\n",
                        rai::Thread::getProcessId(),
                        (double) cpu.totalTime / 1000.0,
                        (double) cpu.userTime / 1000.0,
                        (double) cpu.sysTime / 1000.0,
               (double) ( cpu.userTime + cpu.sysTime ) / 1000.0 );
      for ( i = 0; i < cpu.n; i++ ) {
        rai::Sys::out->printf(
                           "Thread=%u name=%s  usr=%.3f + sys=%.3f = %.3f\n",
                           cpu.task[ i ], cpu.name[ i ],
                           (double) cpu.tuser[ i ] / 1000.0,
                           (double) cpu.tsys[ i ] / 1000.0,
           (double) ( cpu.tuser[ i ] + cpu.tsys[ i ] ) / 1000.0 );
      }
    }
    rai::Sys::out->flush();
  }

  /* use two threads, one for publishing / printing and one for subscribe */
  struct PubThread : public rai::Thread {
    RaiPing2       & me;
    bool             done;
    rai::Mutex     * lock;
    rai::Condition * cond;

    PubThread( RaiPing2 &m ) : rai::Thread( "publish" ), me( m ), done( false ),
                           lock( 0 ), cond( 0 ) {}
    ~PubThread() { if ( this->lock ) delete this->lock;
                   if ( this->cond ) delete this->cond; }
    void init( void ) {
      Thread *me = Thread::self();
      if ( me != NULL )
        this->Thread::setName( me->name, this->Thread::name );
      this->lock = rai::Mutex::create();
      this->cond = rai::Condition::create();
    }
    void signalDone( void ) {
      if ( this->lock != NULL ) {
        this->lock->lock();
        this->done = true;
        this->cond->signal();
        this->lock->unlock();
      }
    }
    void publoop( void ) {
      while ( ! this->me.quit ) {
        try {
          this->me.pubQueue->TimedDispatch( 100 );
        } catch ( RaiException e ) {
          this->me.api->PrintLog( LERROR, e, "pubQueue dispatch" );
        }
      }
    }
    virtual void run( void ) {
      this->publoop();
      this->lock->lock(); /* wait for RaiPing2::close() to get CPU stats */
      while ( ! this->done )
        this->cond->wait( this->lock );
      this->lock->unlock();
      this->exit();
    }
  } pubThread;

  void serviceRun( void ) {
    this->dispatchLoop();
  }

  void dispatchLoop( void ) {
    this->pubThread.init();
    this->pubThread.start();
    while ( ! this->quit ) {
      try {
        this->subQueue->TimedDispatch( 100 );
      } catch ( RaiException e ) {
        this->api->PrintLog( LERROR, e, "subQueue dispatch" );
      }
    }
  }
};
} // namespace

#ifdef RAI_DLL_EMBEDDED
/* this version can be loaded into raicache */

#include "raiapi2_service.h"

/* dll entry point, determined by service file name: rai_service_raiping2.so*/
extern "C" RAI_DLL_EXPORT void
RAIPING2_ServiceInitialize( void )
{
  rai::ServiceFactory * fact = NEW
    T_RaiApiServiceFactory< RaiPing2Args,
      T_RaiApiServiceProto< RaiPing2 > >( "raiping2" );
  rai::ServiceFactory::installService( fact );
}

#else
/* this is cmdline main for static or dynamic loading middlewares */
static RaiPing2 * ping;

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

  rai::Args    args;
  RaiApi     * api = NULL;
  RaiPing2Args pargs;

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
    /* get the ping subject and rate */
    pargs.getArgs( args, true );
    /* get the arguments for the dictionary, useful for parsing dict files
     * locally in the filesystem instead of receiving it on the network */
    RaiApi::GetDictArgs( args );
    /* get the logging, version, help, rc arguments and sets error output */
    args.addDefaults( api->RaiVersion(), "rai_", rai::Sys::err, argv[ 0 ] );

    try {
      /* match command line args, if -help or -version, returns false */
      if ( args.processArgs( argc, argv ) ) {
        ping = NEW RaiPing2();
        /* initialize ping subscriptions, publisher, queue */
        if ( ping->init( api, args ) )
          /* run ping queue dispatch */
          ping->dispatchLoop();
        /* print ping latency summary and close api handles */
        ping->close();
        delete ping;
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
  if ( ping == NULL )
    exit( 1 );
  ping->quit = true;
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
  if ( ping == NULL )
    exit( 1 );
  if ( ping != NULL )
    ping->quit = true;
  return TRUE;
}
#endif
#endif
