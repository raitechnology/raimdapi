/* Copyright (c) 2004 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */

#if ! defined(NO_WHATLIST) && defined(__GNUC__)
static char CVS_ID_raiutil__raisampleutil_cpp[] __attribute__ ((__unused__)) = "$Header$";
#endif /* ! defined(NO_WHATLIST) */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "raiapi.h"
#include "raisampleutil.h"

using namespace rai;
using namespace rai_old; /* v1 api */

Argument::Argument( const char * name, const char * defVal, const char * example, const char * description ) {
  this->name		= name;
  this->defVal		= defVal;
  this->example		= example;
  this->description	= description;
  this->value		= NULL;
  this->next		= NULL;
}

void ArgList::add( Argument * arg ) {
  if( this->first == NULL ) {
    this->first = arg;
  } else {
    this->last->next = arg;
  }
  this->last = arg;
}

Argument * ArgList::find( const char *name ) {
  Argument * curr = this->first;
  /* skip '-' */
  if( name[0] == '-' ) {
    name = name + 1;
  }
  while( curr ) {
    if( strcmp( curr->name, name ) == 0 ) {
      return curr;
    }
    curr = curr->next;
  }
  return NULL;
}

const char * ArgList::getString( const char *name ) {
  Argument * arg;

  if( name == NULL ) {
    return NULL;
  }

  if( ( arg = find( name ) ) == NULL ) {
    return NULL;
  }

  if( arg->value == NULL ) {
    return arg->defVal;
  } else {
     return arg->value;
  }
}

unsigned int ArgList::getUInt( const char *name ) {
  Argument * arg;

  if( name == NULL ) {
    return 0;
  }

  if( ( arg = find( name ) ) == NULL ) {
    return 0;
  }

  if( arg->value == NULL ) {
    return atoi( arg->defVal );
  } else {
    return atoi( arg->value );
  }
}

unsigned long ArgList::getULLong( const char *name ) {
  Argument * arg;

  if( name == NULL ) {
    return 0;
  }

  if( ( arg = find( name ) ) == NULL ) {
    return 0;
  }

  if( arg->value == NULL ) {
    return atol( arg->defVal );
  } else {
    return atol( arg->value );
  }
}

int ArgList::processArgs( int argc, char *argv[] ) {

  Argument	* arg;
  int		  argIdx = 1;

  while( argIdx < argc ) {
    if( strcmp( argv[ argIdx ], "-help" ) == 0 ) {
      this->help();
      return ARG_NOT_FOUND;
    }
    if( strcmp( argv[ argIdx ], "-version" ) == 0 ) {
      this->version();
      return ARG_NOT_FOUND;
    }
    if( ( arg = find( argv[ argIdx ] ) ) ) {
      argIdx++;
      if( argIdx < argc ) {
	arg->value = argv[ argIdx ];
	argIdx++;
      } else {
	Sys::out->printf( "No value for argument %s\n", argv[ argIdx -1 ] );
	return ARG_NOT_FOUND;
      }
    } else {
      Sys::out->printf( "Unknown argument %s\n", argv[ argIdx ] );
      return ARG_NOT_FOUND;
    }
  }
  return ARG_FOUND;
}

void ArgList::help() {
  Argument     * curr = first;
  while( curr ) {
    Sys::out->printf( "  -%-20.20s  %s\n", curr->name, curr->description );
    Sys::out->printf( "   %-20.20s  Default: %s\n", "", curr->defVal == NULL ? "None" : curr->defVal );
    Sys::out->printf( "   %-20.20s  Example: %s\n\n", "", curr->example );
    curr = curr->next;
  }
  Sys::out->flush();
}

void ArgList::version() {
  Sys::out->printf( "%s\n", RaiApi::RaiVersion() );
  Sys::out->flush();
}
