/* Copyright (c) 2003 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_test__pingrv_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#include <string.h>
#include <stdio.h>

#include "raiapi.h"
#include "raisampleutil.h"

using namespace rai;
using namespace rai_old; /* v1 api */

static const char WARN_FASTPRODUCER[] = "_RV.WARN.SYSTEM.CLIENT.FASTPRODUCER";
static const char * prefix_null_check( const char *value ) {
  if ( value != NULL &&
       ( value[ 0 ] == '\0' || ::strcmp( value, "\"\"" ) == 0  ||
         ::strcmp( value, "-" ) == 0 ) )
    value = NULL;
  return value;
}

class Ping {
 public:
  RaiSession   * session;
  RaiSubscribe * subscriber;
  RaiPublish  * Publisher;
  RaiTimer    * startTimer;
  RaiDict     * dataDict;
  bool          done;
  const char  * subject;
  char          PublishSubject[ 1024 ];
  unsigned int  msgsPerSec;
  ullong        msgsPublished,
                msgsClocked,
                msgCount,
                msgSent,
                msgRecvd;
  TimeMSecs     startTime,
                timer;
  double        deltaSum,
                lastDelta;

  Ping( const char *pref, const char *subject,  unsigned int msgsPerSec,  ullong msgCount ) {
    this->session       = NULL;
    this->Publisher     = NULL;
    this->subscriber    = NULL;
    this->startTimer    = NULL;
    this->dataDict      = NULL;
    this->done          = false;
    this->subject       = subject;
    this->msgsPerSec    = msgsPerSec;
    this->msgsPublished = 0;
    this->msgsClocked   = 0;
    this->msgCount      = msgCount;
    this->msgRecvd      = 0;
    this->msgSent       = 0;
    this->startTime     = 0;
    this->timer         = 0;
    this->deltaSum      = 0;
    this->lastDelta     = 0;

    pref = prefix_null_check( pref );
    if ( pref != NULL ) {
      ::strcpy( this->PublishSubject, pref );
      ::strcat( this->PublishSubject, "." );
      ::strcat( this->PublishSubject, subject );
    }
    else {
      ::strcpy( this->PublishSubject, subject );
    }
  };

  static void ping_onMsg( RaiEvent * /* event */,  RaiMsg * raiMsg,  void * closure ) {
    Ping * me = (Ping *) closure;
    double cpms;
    ullong sendTime,
           curTime = Time::getHiresTime( &cpms );

    try {
      if ( raiMsg->Get( "time", sendTime ) ) {
        double delta = (double) ( curTime - sendTime ) / (double) cpms;
        me->deltaSum += delta;
        me->msgRecvd += 1;
        if ( me->msgsPerSec <= 10 ) {
          printf( "%s: cnt=%u time=%.06f us\n",
                  me->subject, (unsigned int) me->msgRecvd,
                  (double) ( curTime - sendTime ) / (double) cpms );
        }
        else {
          if ( me->msgRecvd % me->msgsPerSec == 0 ) {
            printf( "%s: cnt=%u avg time=%.06f us\n",
                    me->subject, (unsigned int) me->msgRecvd,
                    ( me->deltaSum - me->lastDelta ) / (double) me->msgsPerSec );
            me->lastDelta = me->deltaSum;
          }
        }
        if ( me->msgRecvd == me->msgCount )
          me->done = true;
        // set finished flag
      }        
    } catch ( Error e ) {
      printf( "Ping_onMsg: %s.%u: %s\n", e->module, e->status, e->reason );
    }
  }

  void updateClock( void ) {
    TimeMSecs currentTime;

    currentTime = Time::currentTimeMillisecs();
    if ( this->startTime == 0 )
      this->startTime = currentTime;
    else if ( currentTime > this->startTime ) {
      this->msgsClocked = (ullong) ( currentTime - this->startTime ) *
        (ullong) this->msgsPerSec / (ullong) 1000;
    }
  }

  static void  onTimer( RaiSession * /* session */, void * closure ) {
    Ping       * me = (Ping *) closure;
    ullong       time;
    byte         msgBuf[ 1024 ];
    RaiMsg       msg;

    try {
      me->updateClock();
      for ( ; me->msgsPublished < me->msgsClocked;
            me->msgsPublished++ ) {
        msg.InitBuffer( (char *) msgBuf, 0, sizeof( msgBuf ), RV_PROTO,
                        RAIMSG_MEMORY_STATIC );
        time = Time::getHiresTime();
        msg.Append( "time", time );
        me->Publisher->Publish( me->PublishSubject, &msg );
        me->msgSent += 1;
      }
    } catch ( Error e ) {
      printf( "Error publishing message: %s.%u: %s\n", e->module, e->status, e->reason );
    }
  };

  static Ping *create( const char *svcname,  const char *netname,
                       const char *dmname,  const char *pref,
                       const char *subject,  unsigned int msgsPerSec,
                       ullong msgCount ) {
    Ping         * ping;
    char         dictSubject[80];

    try {
      ping = new Ping( pref, subject, msgsPerSec, msgCount );

      RaiApi::RaiOpen( RV7, SASS2 );
      ping->session = NEW RaiSession( svcname, netname, dmname );
      // need to pass a subscription property (like UPDATE )
      ping->subscriber = new RaiSubscribe(ping->session, ping->subject, 
                                          Ping::ping_onMsg, ping, UPDATE );
      ping->Publisher  = new RaiPublish(ping->session );

      if ( ping->Publisher->usesSass ) {
        printf("Uses SASS dict\n");
        ping->dataDict = new RaiDict();
        ::strcpy(dictSubject, "_TIC.REPLY.SASS.DATA.DICTIONARY");
        ping->dataDict->Load( ping->session, dictSubject );
      }
        

      if ( ping->msgsPerSec > 1000 )
        ping->timer = 1;
      else
        ping->timer = 1000 / ping->msgsPerSec;

      ping->startTimer = new RaiTimer( ping->session,  Ping::onTimer, ping->timer, (void *) ping );

      return ping;
    } catch ( Error e ) {
      printf( "Error Init Failed: %s.%u: %s\n", e->module, e->status, e->reason );
      return NULL;
    }
  };

  void run( void ) {
          
    while ( this->done == false ) {
      RaiApi::RaiTimedDispatch( this->session, 1 );
    }
  };

  void close( void ) {

    if ( this->session != NULL ) {
      RaiApi::RaiClose();
      this->session = NULL;
    }
  };

  virtual ~Ping() {};
};


int
main( int argc, char *argv[] )
{
  Argument service(     "service", NULL, "-service 7600",
                        "Service name of RV to connect to" );
  Argument network(     "network", NULL, "-network 172.16.1.0",
                        "Network of RV to connect to" );
  Argument daemon(      "daemon", NULL, "-daemon tcp:7600",
                        "Daemon name of RV to connect to" );
  Argument   perSec(    "perSec", "1", "-perSec 50", "Number of msgs per sec" );
  Argument msgCount(    "msgCount", "0", "-msgCount 1000", "Number of msgs to Publish, "
                       "0 for infinite" );
  Argument prefix(     "prefix", "", "-prefix _TIC.", 
                       "Publish subject prefix, use \"\" for no prefix" );
  Argument subject(    "subject", "PING.REC.TEST.NaE", "-subject RAITEST.a.b.c",
                       "Publish subject to ping" );
  ArgList args;
  Ping * ping;

  Sys::initialize();

  args.add( &network );
  args.add( &service );
  args.add( &daemon );
  args.add( &perSec );
  args.add( &msgCount );
  args.add( &prefix );
  args.add( &subject );

  try {
    if ( args.processArgs( argc, argv ) ) {
      const char        * netname  = args.getString( network.name ),
                        * svcname  = args.getString( service.name ),
                        * dmname   = args.getString( daemon.name ),
                        * pref     = args.getString( prefix.name ),
                        * subj     = args.getString( subject.name );
      unsigned int        persec   = args.getUInt( perSec.name );
      ullong              numMsgs  = args.getULLong( msgCount.name );

      ping = Ping::create( svcname, netname, dmname, pref, subj, persec, numMsgs );
      ping->run();
      delete ping;
    }
  } catch ( Error e ) {
    printf( "Error: %s.%u: %s\n", e->module, e->status, e->reason );
  }

  return 0;
}

