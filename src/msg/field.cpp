/* Copyright (c) 2003 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */
#if ! defined( RAIMSG_DLL_EXP ) && defined( RAI_DLL )
#define RAIMSG_DLL_EXP __declspec(dllexport)
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>

#include "msg/msg.h"
#include "msg/field.h"
#include "msg/dict.h"
#include "msg/defs.h"
#include "msg/subject.h"
#include "msg/sass_const.h"
#include "util/int_bits.h"
#include "util/str_util.h"
#include "stream/io_stream.h"
#include "base/log.h"

using namespace rai;

void
RaiField::ReUse( void )
{
  this->iterMsg  = NULL;
  this->name     = NULL;
  this->nameLen  = 0;
  this->type     = RAIMSG_NODATA;
  this->hintType = RAIMSG_NODATA;
}


RaiField::RaiField( RaiMsg_name name,  RaiMsg_type type,  RaiMsg_size size,
                    RaiMsg_data data )
{
  this->iterMsg  = NULL;
  this->name     = name;
  this->nameLen  = FNAME_LEN( name );
  this->type     = type;
  this->size     = size;
  this->data     = data;
  this->hintType = RAIMSG_NODATA;
}


RaiField::RaiField( RaiMsg_name name,  RaiMsg_type type,  RaiMsg_size size,
                    RaiMsg_data data,  RaiMsg_type hint_type,
                    RaiMsg_size hint_size,  RaiMsg_data hint_data )
{
  this->iterMsg  = NULL;
  this->name     = name;
  this->nameLen  = FNAME_LEN( name );
  this->type     = type;
  this->size     = size;
  this->data     = data;
  this->hintType = hint_type;
  this->hintSize = hint_size;
  this->hintData = hint_data;
}


RaiMsg_size
RaiField::MakeFidName( RaiMsg_name fname,  Rai_u16 fid,  char buf[ 256 ] )

{
  unsigned int len;

  if ( fname == NULL ) {
    Unaligned::endianPutInt( fid, (Rai_u8 *) buf );
    return 2;
  }
  if ( fname[ 0 ] != '\0' ) {
    len = (unsigned int) ::strlen( fname ) + 1;
    if ( len + 2 > 256 )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
    ::memcpy( buf, fname, len );
  }
  else {
    buf[ 0 ] = '\0';
    len = 1;
  }
  Unaligned::endianPutInt( fid, (Rai_u8 *) &buf[ len ] );
  return len + 2;
}


RaiMsg_data
RaiField::Data( void )
{
  RaiMsg_data array;

#if 0 /* NULL terminate strings? */
  if ( this->type == RAIMSG_STRING ) {
    if ( this->iterMsg != NULL && this->iterMsg->proto == RAI_SASS_PROTO ) {
      this->iterMsg->GetDecodedString( this->data, this->size, array );
      return array;
    }
  }
#endif
  if ( this->type == RAIMSG_ARRAY ) {
    /* don't need to align these */
    if ( this->hintType == RAIMSG_STRING || this->hintType == RAIMSG_OPAQUE )
      return this->data;
    /* align and swap */
    if ( this->iterMsg != NULL ) {
      this->iterMsg->GetDecodedArray( this->data, this->hintType,
                                      this->hintSize, this->size, array );
      return array;
    }
  }
  return this->data;
}


void
RaiField::Get( RaiMsg &msg )
{
  if ( this->type == RAIMSG_MESSAGE ) {
    if ( this->tempMsg.proto == RV_PROTO )
      msg.InitSubMessage( this->tempMsg.msgBuf, this->tempMsg.msgSize,
                          this->tempMsg.parent, this->tempMsg.proto,
                          this->tempMsg.isDynamic );
    else
      msg.InitSubMessage( &this->tempMsg.msgBuf[ this->tempMsg.msgStart ],
                          this->tempMsg.msgSize - this->tempMsg.msgStart,
                          this->tempMsg.parent, this->tempMsg.proto,
                          RAIMSG_MEMORY_FIXED );
  }
  else if ( this->iterMsg != NULL && this->fieldEnd > this->fieldStart ) {
    Rai_u8    * start = &this->iterMsg->msgBuf[ this->fieldStart ];
    RaiMsg_size size  = this->fieldEnd - this->fieldStart;
    msg.InitSubMessage( start, size, this->iterMsg,
                        this->iterMsg->proto, RAIMSG_MEMORY_FIXED );
  }
  else {
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );
  }
}


RaiMsg_size
RaiField::PackSize( RaiMsg_protocol proto )
{
  RaiMsg_size         packSize;
  const RaiMsg_dict * entry;

  if ( this->type == RAIMSG_NODATA )
    return 0;

  switch ( proto ) {
    case RAIMSG_PROTO:
      packSize = 1 + this->nameLen;  /* nameLen + name */
      packSize += 1;                                /* type */
      if ( this->type == RAIMSG_MESSAGE || this->size > 0xffU )
        packSize += 4;
      else
        packSize += 1;                              /* size */
      packSize += this->size;                       /* data */
      if ( this->type == RAIMSG_PARTIAL ) {         /* offset of partial */
        packSize += 2;
        if ( this->hintSize > 0xffU )
          packSize += 3;
#if 0
        if ( this->hintSize != 0 ) {
          packSize += ( this->hintSize <= 0xffU ) ? 1 :
                      ( this->hintSize <= 0xffffU ) ? 2 : 4;
        }
#endif
      }
      else if ( this->type == RAIMSG_ARRAY )      /* type and size of element */
        packSize += 2;
      else if ( this->hintType != RAIMSG_NODATA )
        packSize += 1 +                                  /* hintType */
                    ( this->hintSize > 0xffU ? 4 : 1 ) + /* hintSize */
                    this->hintSize;                      /* hintData */
      break;

    case RV_PROTO:
      packSize = 1 + this->nameLen;  /* nameLen + name */
      packSize += 1;                 /* type */

      switch ( this->type ) {
        case RAIMSG_OPAQUE:
        case RAIMSG_STRING:
          if ( ( this->hintType == RAIMSG_UINT ||
                 this->hintType == RAIMSG_INT ) ) {
            Rai_u8 rvType;
            if ( this->hintSize != 0 )
              this->GetHint( rvType );
            else
              rvType = 0;

            if ( rvType == RAI_RV_SUBJECT ) {
              if ( this->type == RAIMSG_STRING ) {
                unsigned int tmp;
                size = RvSubject::validate( (char *) this->data, tmp );
                packSize += ( size < MAX_RV_SHORT_SIZE ) ? 3 : 5;
                packSize += size;
              }
              else {
                packSize += ( this->size < MAX_RV_SHORT_SIZE ) ? 3 : 5;
                packSize += this->size;
              }
              break;
            }
          }

        /* FALLTHRU */
        case RAIMSG_PARTIAL:
        case RAIMSG_ARRAY:
          packSize += ( this->size < RAI_RV_TINY_SIZE ) ? 1 :
                      ( this->size < MAX_RV_SHORT_SIZE ) ? 3 : 5;
          packSize += this->size;
          break;

        case RAIMSG_MESSAGE:
          packSize += 1 + this->size;  /* long-size + size + magic */
          break;

        default:
          packSize += 1 + this->size;  /* size */
          break;
      }
      break;

    case CI_SASS_FORM_PROTO:
    case CI_SASS_PROTO:
    case TIB_SASS_FORM_PROTO:
    case TIB_SASS_PROTO:
      if ( DataDictionary == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_DICTIONARY );
      entry = DataDictionary->getEntry( this->name, this->nameLen );
      if ( entry == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FNAME );
      if ( entry->partial )
        packSize = 6 + ( ( this->size + 1U ) & ~1U );
      else
        packSize = entry->packSize();
      break;

    default:
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );
  }

  return packSize;
}


void
RaiField::Pack( RaiMsg_protocol proto,  Rai_u8 *to_ptr )

{
  RaiMsg            * msg;
  Rai_u8              type;
  Rai_u16           * u16ar;
  Rai_u32             i,
                      count,
                    * u32ar;
  Rai_u64           * u64ar;
  const RaiMsg_dict * entry;

  switch ( proto ) {
    case RAIMSG_PROTO:
      /* encode the name */
      if ( this->nameLen == 0 )
        *to_ptr++ = 0;
      else {
        *to_ptr = (char) (Rai_u8) this->nameLen;
        ::memcpy( &to_ptr[ 1 ], this->name, this->nameLen );
        to_ptr = &to_ptr[ 1 + this->nameLen ];
      }

      /* if size is 4 bytes and not 1 byte, add 0x80 flag to type
       * if has hint data, add 0x40 flag to type */
      type = (Rai_u8) this->type;
      if ( this->size > 0xffU || this->type == RAIMSG_MESSAGE )
        type |= 0x80;
      if ( this->hintType != RAIMSG_NODATA || this->type == RAIMSG_PARTIAL )
        type |= 0x40;

      /* encode the type and the size */
      *to_ptr++ = (char) type;
      if ( ( type & 0x80 ) == 0 )
        *to_ptr++ = (Rai_u8) this->size;
      else {
        Unaligned::endianPutInt( this->size, (Rai_u8 *) to_ptr );
        to_ptr = &to_ptr[ 4 ];
      }

      /* encode the data */
      switch ( this->type ) {
        case RAIMSG_STRING:
        case RAIMSG_OPAQUE:
          ::memcpy( to_ptr, this->data, this->size );
          to_ptr = &to_ptr[ this->size ];
          break;

        case RAIMSG_BOOLEAN:
        case RAIMSG_INT:
        case RAIMSG_UINT:
        case RAIMSG_REAL:
        case RAIMSG_IPDATA:
          if ( this->size == 4 ) {
            ::memcpy( &to_ptr[ 0 ], this->data, 4 );
            to_ptr = &to_ptr[ 4 ];
            break;
          }
          if ( this->size == 2 ) {
            ::memcpy( &to_ptr[ 0 ], this->data, 2 );
            to_ptr = &to_ptr[ 2 ];
            break;
          }
          if ( this->size == 1 ) {
            to_ptr[ 0 ] = *(Rai_u8 *) this->data;
            to_ptr = &to_ptr[ 1 ];
            break;
          }
          if ( this->size == 8 ) {
            ::memcpy( &to_ptr[ 0 ], this->data, 8 );
            to_ptr = &to_ptr[ 8 ];
            break;
          }
          if ( this->size == 0 )
            break;

        /* FALLTHRU */
        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_TYPE );

        case RAIMSG_PARTIAL:
          ::memcpy( to_ptr, this->data, this->size );
          to_ptr = &to_ptr[ this->size ];
          if ( this->hintSize <= 0xffU ) {
            *to_ptr++ = RAIMSG_UINT;
            *to_ptr++ = (Rai_u8) this->hintSize;
          }
          else {
            *to_ptr++ = 0x80U | RAIMSG_UINT;
            Unaligned::endianPutInt( this->hintSize, (Rai_u8 *) to_ptr );
            to_ptr = &to_ptr[ 4 ];
          }
#if 0
          /* offset */
          *to_ptr++ = RAIMSG_UINT;
          if ( this->hintSize == 0 ) 
            *to_ptr++ = 0;
          else {
            if ( this->hintSize <= 0xffU ) {
              *to_ptr++ = 1;
              *to_ptr++ = (Rai_u8) this->hintSize;
            }
            else if ( this->hintSize <= 0xffffU ) {
              Rai_u16 u16;
              *to_ptr++ = 2;
              u16 = (Rai_u16) this->hintSize;
              Unaligned::endianPutInt( u16, (Rai_u8 *) to_ptr );
              to_ptr = &to_ptr[ 2 ];
            }
            else {
              *to_ptr++ = 4;
              Unaligned::endianPutInt( this->hintSize, (Rai_u8 *) to_ptr );
              to_ptr = &to_ptr[ 4 ];
            }
          }
#endif
          break;

        case RAIMSG_ARRAY:
          /* no need to swap array elements if big endian already */
          if ( ! Aligned::isLittleEndian || this->hintSize == 1 ||
               this->hintType == RAIMSG_STRING ||
               this->hintType == RAIMSG_OPAQUE ||
               ( this->hintType == RAIMSG_IPDATA && this->hintSize == 4 ) ) {
            ::memcpy( to_ptr, this->data, this->size );
            to_ptr = &to_ptr[ this->size ];
          }
          else {
            switch ( this->hintType ) { /* element type */
              case RAIMSG_BOOLEAN:
              case RAIMSG_INT:
              case RAIMSG_UINT:
              case RAIMSG_REAL:
              case RAIMSG_IPDATA:
                switch ( this->hintSize ) { /* element size */
                  case 2:
                    u16ar = (Rai_u16 *) this->data;
                    count = this->size / 2;
                    for ( i = 0; i < count; i++ ) {
                      Unaligned::endianPutInt( u16ar[ i ], (Rai_u8 *) to_ptr );
                      to_ptr = &to_ptr[ 2 ];
                    }
                    break;
                  case 4:
                    u32ar = (Rai_u32 *) this->data;
                    count = this->size / 4;
                    for ( i = 0; i < count; i++ ) {
                      Unaligned::endianPutInt( u32ar[ i ], (Rai_u8 *) to_ptr );
                      to_ptr = &to_ptr[ 4 ];
                    }
                    break;
                  case 8:
                    u64ar = (Rai_u64 *) this->data;
                    count = this->size / 8;
                    for ( i = 0; i < count; i++ ) {
                      Unaligned::endianPutInt( u64ar[ i ], (Rai_u8 *) to_ptr );
                      to_ptr = &to_ptr[ 8 ];
                    }
                    break;
                  default:
                    throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
                }
                break;
              default:
                throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
            }
          }
          *to_ptr++ = (Rai_u8) this->hintType;
          *to_ptr++ = (Rai_u8) this->hintSize;
          break;

        case RAIMSG_MESSAGE:
          msg = (RaiMsg *) this->data;
          ::memcpy( to_ptr, &msg->msgBuf[ msg->msgStart ], this->size );
          to_ptr = &to_ptr[ this->size ];
          break;
      }

      /* encode the hint data */
      if ( ( type & 0x40 ) != 0 && this->type != RAIMSG_ARRAY &&
                                   this->type != RAIMSG_PARTIAL ) {
        type = (Rai_u8) this->hintType;
        if ( this->hintSize > 0xffU )
          type |= 0x80;

        *to_ptr++ = type;
        if ( ( type & 0x80 ) == 0 )
          *to_ptr++ = (Rai_u8) this->hintSize;
        else {
          Unaligned::endianPutInt( this->hintSize, (Rai_u8 *) to_ptr );
          to_ptr = &to_ptr[ 4 ];
        }

        switch ( this->hintType ) {
          case RAIMSG_STRING:
          case RAIMSG_OPAQUE:
            ::memcpy( to_ptr, this->hintData, this->hintSize );
            break;
          case RAIMSG_BOOLEAN:
          case RAIMSG_INT:
          case RAIMSG_UINT:
          case RAIMSG_REAL:
          case RAIMSG_IPDATA:
            if ( this->hintSize == 4 ) {
              ::memcpy( &to_ptr[ 0 ], this->hintData, 4 );
              break;
            }
            if ( this->hintSize == 2 ) {
              ::memcpy( &to_ptr[ 0 ], this->hintData, 2 );
              break;
            }
            if ( this->hintSize == 1 ) {
              to_ptr[ 0 ] = *(Rai_u8 *) this->hintData;
              break;
            }
            if ( this->hintSize == 8 ) {
              ::memcpy( &to_ptr[ 0 ], this->hintData, 8 );
              break;
            }
            if ( this->hintSize == 0 )
              break;
          /* FALLTHRU */
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
        }
      }
      break;

    case RV_PROTO:
      /* encode the name */
      if ( this->nameLen == 0 )
        *to_ptr++ = 0;
      else {
        *to_ptr = (char) (Rai_u8) this->nameLen;
        ::memcpy( &to_ptr[ 1 ], this->name, this->nameLen );
        to_ptr = &to_ptr[ 1 + this->nameLen ];
      }

      switch ( this->type ) {
        case RAIMSG_MESSAGE:
          to_ptr[ 0 ] = (Rai_u8) RAI_RV_RVMSG;
          to_ptr[ 1 ] = (Rai_u8) RAI_RV_LONG_SIZE;

          Unaligned::endianPutInt( this->size == 0 ? 8 : this->size,
                                   (Rai_u8 *) &to_ptr[ 2 ] );
          Unaligned::endianPutInt( RAIMSG_MAGIC_RV, (Rai_u8 *) &to_ptr[ 6 ] );
          if ( this->size > 0 ) {
            msg = (RaiMsg *) this->data;
            ::memcpy( &to_ptr[ 10 ], &msg->msgBuf[ msg->msgStart + 8 ],
                      this->size - 8 );
          }
          break;

        case RAIMSG_PARTIAL:
          to_ptr[ 0 ] = (Rai_u8) RAI_RV_OPAQUE;
          goto pack_rv_data;

        case RAIMSG_OPAQUE:
          to_ptr[ 0 ] = (Rai_u8) RAI_RV_OPAQUE;
          goto check_rv_type;

        case RAIMSG_STRING: {
          byte        subjBuf[ SassConst::MAX_SUBJECT_LEN ];
          RaiMsg_data rvData;
          RaiMsg_size rvSize;

          to_ptr[ 0 ] = (Rai_u8) RAI_RV_STRING;

        check_rv_type:;
          if ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) {
            Rai_u8 rvType;
            this->GetHint( rvType );

            rvSize = this->size;
            rvData = this->data;

            if ( rvType == RAI_RV_SUBJECT ) {
              if ( this->type == RAIMSG_STRING ) {
                RvSubject subj;
                subj.encode( (char *) this->data, subjBuf,
                             sizeof( subjBuf ) );
                rvData = (RaiMsg_data) subjBuf;
                rvSize = subj.length();
              }
              to_ptr[ 0 ] = (Rai_u8) RAI_RV_SUBJECT;
            }
            /*else if ( rvType > RAI_RV_RVMSG && rvType <= RAI_RV_ENCRYPTED ) {
              to_ptr[ 0 ] = rvType;
            }*/
          }
          else {
        pack_rv_data:;
            rvSize = this->size;
            rvData = this->data;
          }

          if ( rvSize < RAI_RV_TINY_SIZE &&
               to_ptr[ 0 ] != (Rai_u8) RAI_RV_SUBJECT ) {
            to_ptr[ 1 ] = (Rai_u8) rvSize;
            to_ptr      = &to_ptr[ 2 ];
          }
          else if ( rvSize < MAX_RV_SHORT_SIZE ) {
            to_ptr[ 1 ] = (Rai_u8) RAI_RV_SHORT_SIZE;
            Unaligned::endianPutInt( (Rai_u16) ( rvSize + 2 ),
                                     (Rai_u8 *) &to_ptr[ 2 ] );
            to_ptr = &to_ptr[ 4 ];
          }
          else {
            to_ptr[ 1 ] = (Rai_u8) RAI_RV_LONG_SIZE;
            Unaligned::endianPutInt( (Rai_u32) ( rvSize + 4 ),
                                     (Rai_u8 *) &to_ptr[ 2 ] );
            to_ptr = &to_ptr[ 6 ];
          }
          ::memcpy( to_ptr, rvData, rvSize );
          break;
        }
        case RAIMSG_BOOLEAN:
        case RAIMSG_INT:
        case RAIMSG_UINT:
        case RAIMSG_REAL:
        case RAIMSG_IPDATA:
          //if ( this->hintType != RAIMSG_INT && this->hintType != RAIMSG_UINT )
            to_ptr[ 0 ] = (Rai_u8) raiMsgTypeToRvType[ this->type ];
          /*else {
            this->GetHint( to_ptr[ 0 ] );
            if ( to_ptr[ 0 ] <= RAI_RV_RVMSG || to_ptr[ 0 ] > RAI_RV_ENCRYPTED )
              to_ptr[ 0 ] = (Rai_u8) raiMsgTypeToRvType[ this->type ];
          }*/

          if ( this->size == 4 ) {
            to_ptr[ 1 ] = 4;
            ::memcpy( &to_ptr[ 2 ], this->data, 4 );
            break;
          }
          if ( this->size == 2 ) {
            to_ptr[ 1 ] = 2;
            ::memcpy( &to_ptr[ 2 ], this->data, 2 );
            break;
          }
          if ( this->size == 1 ) {
            to_ptr[ 1 ] = 1;
            to_ptr[ 2 ] = *(Rai_u8 *) this->data;
            break;
          }
          if ( this->size == 8 ) {
            to_ptr[ 1 ] = 8;
            ::memcpy( &to_ptr[ 2 ], this->data, 8 );
            break;
          }
          if ( this->size == 0 ) {
            to_ptr[ 1 ] = 0;
            break;
          }
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_RV );

        case RAIMSG_ARRAY:
          switch ( this->hintType ) {
            case RAIMSG_REAL:
              switch ( this->hintSize ) {
                case 4: to_ptr[ 0 ] = RAI_RV_ARRAY_F32; break;
                case 8: to_ptr[ 0 ] = RAI_RV_ARRAY_F64; break;
                default:
                  throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
              }
              break;
            case RAIMSG_INT:
              switch ( this->hintSize ) {
                case 1: to_ptr[ 0 ] = RAI_RV_ARRAY_I8; break;
                case 2: to_ptr[ 0 ] = RAI_RV_ARRAY_I16; break;
                case 4: to_ptr[ 0 ] = RAI_RV_ARRAY_I32; break;
                case 8: to_ptr[ 0 ] = RAI_RV_ARRAY_I64; break;
                default:
                  throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
              }
              break;
            case RAIMSG_UINT:
              switch ( this->hintSize ) {
                case 1: to_ptr[ 0 ] = RAI_RV_ARRAY_U8; break;
                case 2: to_ptr[ 0 ] = RAI_RV_ARRAY_U16; break;
                case 4: to_ptr[ 0 ] = RAI_RV_ARRAY_U32; break;
                case 8: to_ptr[ 0 ] = RAI_RV_ARRAY_U64; break;
                default:
                  throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
              }
              break;
            default:
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_RV );
          }
          if ( this->size >= RAI_RV_TINY_SIZE ) {
            if ( this->size >= MAX_RV_SHORT_SIZE ) {
              to_ptr[ 1 ] = RAI_RV_LONG_SIZE;
              Unaligned::endianPutInt( (unsigned int) ( this->size + 4 ),
                                       (Rai_u8 *) &to_ptr[ 2 ] );
              to_ptr = &to_ptr[ 6 ];
            }
            else {
              to_ptr[ 1 ] = RAI_RV_SHORT_SIZE;
              Unaligned::endianPutInt( (unsigned short) ( this->size + 2 ),
                                       (Rai_u8 *) &to_ptr[ 2 ] );
              to_ptr = &to_ptr[ 4 ];
            }
          }
          else {
            to_ptr[ 1 ] = (Rai_u8) this->size;
            to_ptr      = &to_ptr[ 2 ];
          }

          /* no need to swap array elements if big endian already */
          if ( ! Aligned::isLittleEndian || this->hintSize == 1 ) {
            ::memcpy( to_ptr, this->data, this->size );
            to_ptr = &to_ptr[ this->size ];
          }
          else {
            switch ( this->hintSize ) { /* element size */
              case 2:
                u16ar = (Rai_u16 *) this->data;
                count = this->size / 2;
                for ( i = 0; i < count; i++ ) {
                  Unaligned::endianPutInt( u16ar[ i ], (Rai_u8 *) to_ptr );
                  to_ptr = &to_ptr[ 2 ];
                }
                break;
              case 4:
                u32ar = (Rai_u32 *) this->data;
                count = this->size / 4;
                for ( i = 0; i < count; i++ ) {
                  Unaligned::endianPutInt( u32ar[ i ], (Rai_u8 *) to_ptr );
                  to_ptr = &to_ptr[ 4 ];
                }
                break;
              case 8:
                u64ar = (Rai_u64 *) this->data;
                count = this->size / 8;
                for ( i = 0; i < count; i++ ) {
                  Unaligned::endianPutInt( u64ar[ i ], (Rai_u8 *) to_ptr );
                  to_ptr = &to_ptr[ 8 ];
                }
                break;
              default:
                throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
            }
          }
          break;

        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_HINT );
      }
      break;

    case CI_SASS_FORM_PROTO:
    case TIB_SASS_FORM_PROTO:
    case CI_SASS_PROTO:
    case TIB_SASS_PROTO:
      if ( DataDictionary == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_DICTIONARY );
      entry = DataDictionary->getEntry( this->name, this->nameLen );
      if ( entry == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FNAME );
      Unaligned::endianPutInt( (Rai_u16) ( entry->fid | FID_CTRL ),
                               (Rai_u8 *) to_ptr );
      entry->pack( *this, to_ptr );
      break;

    default:
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );
  }
}


void
RaiField::UnPack( Rai_u8 *from_ptr )
{
#if defined( __ICC ) && __ICC == 600
  /* disable: invalid type conversion: "unsigned long" to "char *" */
  #pragma warning(disable:171)
#endif
  this->UnPack( RAIMSG_PROTO, from_ptr, 0x7fffffff );
}


Rai_u8 *
RaiField::UnPack( RaiMsg_protocol proto,  Rai_u8 *from_ptr,
                  unsigned int length )
{
  static Rai_u8 subjectType   = (Rai_u8) RAI_RV_SUBJECT;
  static Rai_u8 encryptedType = (Rai_u8) RAI_RV_ENCRYPTED;
  RaiRvMsg_type       rvType;
  Rai_u8              type;
  Rai_u32             u32;
  Rai_u16             u16;
  const RaiMsg_dict * entry;
  Rai_u16             fid;

  if ( length == 0 )
    throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );

  switch ( proto ) {

    case RAIMSG_PROTO:
      /* get the name */
      this->nameLen = (RaiMsg_size) (Rai_u8) *from_ptr;
      if ( this->nameLen == 0 )
        this->name = NULL;
      else
        this->name = (char *) &from_ptr[ 1 ];

      if ( length < this->nameLen + 3 )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
      length  -= this->nameLen + 3;
      from_ptr = &from_ptr[ this->nameLen + 1 ];

      /* get the type */
      type = (Rai_u8) *from_ptr++;
      this->type = (RaiMsg_type) ( type & 0xf );
      this->data = NULL;

      /* get the size */
      if ( ( type & 0x80 ) == 0 ) {
        this->size = (Rai_u32) (Rai_u8) *from_ptr++;
      }
      else {
        if ( length < 3 )
          throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
        length -= 3;
        Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->size );
        from_ptr = &from_ptr[ 4 ];
      }

      if ( length < this->size )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
      length -= this->size;
      this->hintType = RAIMSG_NODATA;

      /* decode the data */
      switch ( this->type ) {
        case RAIMSG_STRING:
        case RAIMSG_OPAQUE:
          if ( this->size == 0 )
            this->data = NULL;
          else {
            this->data = from_ptr;
            from_ptr = &from_ptr[ this->size ];
          }
          break;

        case RAIMSG_IPDATA:
          if ( this->size == 0 )
            this->data = NULL;
          else if ( this->size != 2 && this->size != 4 )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );

        /* FALLTHRU */
        case RAIMSG_INT:
        case RAIMSG_UINT:
          if ( this->size == 2 ) {
            this->data = from_ptr;
            from_ptr = &from_ptr[ 2 ];
            break;
          }
        /* FALLTHRU */
        case RAIMSG_REAL:
          if ( this->size == 4 ) {
            this->data = from_ptr;
            from_ptr = &from_ptr[ 4 ];
            break;
          }
          if ( this->size == 8 ) {
            this->data = from_ptr;
            from_ptr = &from_ptr[ 8 ];
            break;
          }
          if ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) {
        case RAIMSG_BOOLEAN:
            if ( this->size == 1 ) {
              this->data = from_ptr;
              from_ptr++;
              break;
            }
          }
          if ( this->size == 0 ) {
            this->data = NULL;
            break;
          }
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );

        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_TYPE );

        case RAIMSG_PARTIAL:
          if ( length < 2 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 2;
          this->data = from_ptr;
          from_ptr = &from_ptr[ this->size ];
          /* the hint data type */
          type     = (Rai_u8) *from_ptr++;

          /* decode the hint data size */
          if ( ( type & 0x80U ) == 0 ) {
            this->hintSize = (RaiMsg_size) (Rai_u8) *from_ptr++;
          }
          else {
            if ( length < 3 )
              throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
            length -= 3;
            Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->hintSize );
            from_ptr = &from_ptr[ 4 ];
          }
          break;

        case RAIMSG_ARRAY:
          if ( length < 2 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 2;
          this->data = from_ptr;
          from_ptr = &from_ptr[ this->size ];
          this->hintType = (RaiMsg_type) (Rai_u8) *from_ptr++;
          this->hintSize = (RaiMsg_size) (Rai_u8) *from_ptr++;

          if ( ! RaiField::isValidArrayType( this->hintType, this->hintSize ) )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
          break;

        case RAIMSG_MESSAGE:
          this->data = &this->tempMsg;
          this->tempMsg.InitSubMessage( from_ptr, this->size, this->iterMsg,
                                        RAIMSG_PROTO, RAIMSG_MEMORY_STATIC );
          from_ptr = &from_ptr[ this->size ];
          break;
      }

      /* decode the hint data, if any */
      if ( ( type & 0x40 ) != 0 && this->type != RAIMSG_ARRAY &&
                                   this->type != RAIMSG_PARTIAL ) {
        if ( length == 0 )
          throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
        length -= 1;

        /* the hint data type */
        type = (Rai_u8) *from_ptr++;
        this->hintType = (RaiMsg_type) ( type & 0xf );

        /* decode the hint data size */
        if ( ( type & 0x80 ) == 0 ) {
          if ( length == 0 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 1;
          this->hintSize = (RaiMsg_size) (Rai_u8) *from_ptr++;
        }
        else {
          if ( length < 4 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 4;
          Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->hintSize );
          from_ptr = &from_ptr[ 4 ];
        }

        if ( length < this->hintSize )
          throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
        length -= this->hintSize;

        switch ( this->hintType ) {
          case RAIMSG_STRING:
          case RAIMSG_OPAQUE:
            if ( this->hintSize == 0 )
              this->hintData = NULL;
            else {
              this->hintData = from_ptr;
              from_ptr = &from_ptr[ this->hintSize ];
            }
            break;

          case RAIMSG_BOOLEAN:
          case RAIMSG_INT:
          case RAIMSG_UINT:
          case RAIMSG_REAL:
          case RAIMSG_IPDATA:
            if ( ! RaiField::isValidMachineType( this->hintType,
                                                 this->hintSize ) )
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
            if ( this->hintSize == 0 )
              this->hintData = NULL;
            else {
              this->hintData = from_ptr;
              from_ptr = &from_ptr[ this->hintSize ];
            }
            break;

          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_TYPE );
        }
      }
      return from_ptr;

    case RV_PROTO:
      /* get the name */
      this->nameLen = (RaiMsg_size) (Rai_u8) *from_ptr;
      if ( nameLen == 0 )
        this->name = NULL;
      else
        this->name = (char *) &from_ptr[ 1 ];

      if ( length < this->nameLen + 2 )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
      length  -= this->nameLen + 2;
      from_ptr = &from_ptr[ this->nameLen + 1 ];

      rvType = (RaiRvMsg_type) (Rai_u8) *from_ptr++;
      this->hintType = RAIMSG_NODATA;

      switch ( rvType ) {
        case RAI_RV_RVMSG:
          if ( length < 1 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 1;

          if ( (RaiRvMsg_typesize) (Rai_u8) *from_ptr++ != RAI_RV_LONG_SIZE )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RV_SIZE );
          Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->size );

          if ( length < this->size )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );

          Unaligned::endianGetInt( (Rai_u8 *) &from_ptr[ 4 ], u32 );
          if ( u32 != RAIMSG_MAGIC_RV )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RV_MAGIC );

          this->type = RAIMSG_MESSAGE;
          this->data = &this->tempMsg;
          this->tempMsg.InitSubMessage( from_ptr, this->size, this->iterMsg,
                                        RV_PROTO, RAIMSG_MEMORY_STATIC );
          from_ptr = &from_ptr[ this->size ];
          break;

        case RAI_RV_STRING:
          this->type = RAIMSG_STRING;
          if ( 0 ) {
        case RAI_RV_SUBJECT:
            this->type     = RAIMSG_OPAQUE;
            this->hintType = RAIMSG_UINT;
            this->hintData = &subjectType;
            this->hintSize = 1;
            if ( 0 ) {
        case RAI_RV_ENCRYPTED:
              this->type     = RAIMSG_OPAQUE;
              this->hintType = RAIMSG_UINT;
              this->hintData = &encryptedType;
              this->hintSize = 1;
              if ( 0 ) {
        case RAI_RV_OPAQUE:
                this->type = RAIMSG_OPAQUE;
              }
            }
          }
          if ( length == 0 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 1;

          switch ( (RaiRvMsg_typesize) (Rai_u8) *from_ptr++ ) {
            case RAI_RV_LONG_SIZE:
              if ( length < 4 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->size );
              u32 = 4;
              break;

            case RAI_RV_SHORT_SIZE:
              if ( length < 2 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              Unaligned::endianGetInt( (Rai_u8 *) from_ptr, u16 );
              this->size = (RaiMsg_size) u16;
              u32 = 2;
              break;

            case RAI_RV_TINY_SIZE:
              if ( length < 1 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              this->size = (RaiMsg_size) (Rai_u8) *from_ptr;
              u32 = 1;
              break;

            default:
              this->size = (RaiMsg_size) (Rai_u8) from_ptr[ -1 ];
              u32 = 0;
              break;
              /*throw RaiMsgErr::getErr( RaiMsgErr::BAD_RV_SIZE );*/
          }
          if ( this->size == 0 )
            this->data = NULL;
          else
            this->data = &from_ptr[ u32 ];
          from_ptr = &from_ptr[ this->size ];
          if ( length < this->size || u32 > this->size )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length     -= this->size;
          this->size -= u32;
          break;

        case RAI_RV_DATETIME:
        case RAI_RV_BOOLEAN:
        case RAI_RV_IPDATA:
        case RAI_RV_INT:
        case RAI_RV_UINT:
        case RAI_RV_REAL:
          this->type = rvTypeToRaiMsgType[ rvType ];
          if ( length == 0 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= 1;
          this->size = (RaiMsg_size) (Rai_u8) *from_ptr++;
          if ( ! RaiField::isValidMachineType( this->type, this->size ) )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_RV_MTYPE );
          if ( this->size == 0 )
            this->data = NULL;
          else {
            this->data = from_ptr;
            from_ptr   = &from_ptr[ this->size ];
          }
          if ( length < this->size )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length -= this->size;
          break;

        case RAI_RV_ARRAY_I8:
        case RAI_RV_ARRAY_U8:
        case RAI_RV_ARRAY_I16:
        case RAI_RV_ARRAY_U16:
        case RAI_RV_ARRAY_I32:
        case RAI_RV_ARRAY_U32:
        case RAI_RV_ARRAY_I64:
        case RAI_RV_ARRAY_U64:
        case RAI_RV_ARRAY_F32:
        case RAI_RV_ARRAY_F64:
          this->type = RAIMSG_ARRAY;
          if ( rvType < RAI_RV_ARRAY_F32 ) {
            this->hintType = ( ( rvType - RAI_RV_ARRAY_I8 ) & 1 ) ?
                             RAIMSG_UINT : RAIMSG_INT;
            this->hintSize = 1U << ( ( rvType - RAI_RV_ARRAY_I8 ) / 2 );
          }
          else {
            this->hintType = RAIMSG_REAL;
            if ( rvType == RAI_RV_ARRAY_F32 )
              this->hintSize = 4;
            else
              this->hintSize = 8;
          }

          if ( length == 0 )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );

          this->size = (RaiMsg_size) (Rai_u8) *from_ptr++;
          switch ( (RaiRvMsg_typesize) this->size ) {
            case RAI_RV_LONG_SIZE:
              u32 = 4;
              if ( length <= 4 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              Unaligned::endianGetInt( (Rai_u8 *) from_ptr, this->size );
              break;

            case RAI_RV_SHORT_SIZE:
              u32 = 2;
              if ( length <= 2 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              Unaligned::endianGetInt( (Rai_u8 *) from_ptr, u16 );
              this->size = (RaiMsg_size) u16;
              break;

            case RAI_RV_TINY_SIZE:
              u32 = 1;
              if ( length <= 1 )
                throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
              this->size = (RaiMsg_size) (Rai_u8) *from_ptr;
              break;

            default:
              u32 = 0;
              break;
          }

          if ( this->size != 0 )
            this->data = &from_ptr[ u32 ];
          else
            this->data = NULL;
          from_ptr = &from_ptr[ this->size ];
          if ( length < this->size || u32 > this->size )
            throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
          length     -= this->size;
          this->size -= u32;
          break;

        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RV_TYPE );
      }
      return from_ptr;

    case CI_SASS_FORM_PROTO:
    case CI_SASS_PROTO:
    case TIB_SASS_FORM_PROTO:
    case TIB_SASS_PROTO:
      if ( length < 2 )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_FIELD );
      Unaligned::endianGetInt( (Rai_u8 *) from_ptr, fid );

      if ( DataDictionary == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_DICTIONARY );
      entry = DataDictionary->getEntry( fid );
      if ( entry == NULL ) {
        if ( fid == 0 && length == 2 )
          throw RaiMsgErr::getErr( RaiMsgErr::NULL_DICT_FID );
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }
      return entry->unpack( *this, from_ptr, length );

    default:
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );
  }
}


void
RaiField::Update( RaiField *field_ptr )
{
  this->name    = field_ptr->name;
  this->nameLen = field_ptr->nameLen;

  switch ( field_ptr->type ) {
    default:
    case RAIMSG_NODATA:
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_TYPE );

    case RAIMSG_STRING:
    case RAIMSG_OPAQUE:
      this->type = field_ptr->type;
      this->size = field_ptr->size;
      this->data = field_ptr->data;
      break;

    case RAIMSG_BOOLEAN:
    case RAIMSG_INT:
    case RAIMSG_UINT:
    case RAIMSG_REAL:
    case RAIMSG_IPDATA:
      if ( ! RaiField::isValidMachineType( field_ptr->type,
                                           field_ptr->size ) )
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
      this->type = field_ptr->type;
      this->size = field_ptr->size;
      this->data = &this->updateData;
      ::memcpy( this->data, field_ptr->data, this->size );
      break;

    case RAIMSG_ARRAY:
      if ( ! RaiField::isValidArrayType( field_ptr->hintType,
                                         field_ptr->hintSize ) )
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
      this->type     = RAIMSG_ARRAY;
      this->size     = field_ptr->size;
      this->data     = field_ptr->data;
      this->hintType = field_ptr->hintType;
      this->hintSize = field_ptr->hintSize;
      break;

    case RAIMSG_PARTIAL:
      this->type     = RAIMSG_PARTIAL;
      this->size     = field_ptr->size;
      this->data     = field_ptr->data;
      this->hintType = RAIMSG_NODATA;
      this->hintSize = field_ptr->hintSize;
      break;

    case RAIMSG_MESSAGE:
      this->Update( field_ptr->name, (RaiMsg *) field_ptr->data );
      break;
  }

  if ( field_ptr->type != RAIMSG_ARRAY && field_ptr->type != RAIMSG_PARTIAL ) {
    switch ( field_ptr->hintType ) {
      default:
        this->hintType = RAIMSG_NODATA;
        break;

      case RAIMSG_STRING:
      case RAIMSG_OPAQUE:
        this->hintType = field_ptr->hintType;
        this->hintSize = field_ptr->hintSize;
        this->hintData = field_ptr->hintData;
        break;

      case RAIMSG_BOOLEAN:
      case RAIMSG_INT:
      case RAIMSG_UINT:
      case RAIMSG_REAL:
      case RAIMSG_IPDATA:
        if ( ! RaiField::isValidMachineType( field_ptr->hintType,
                                             field_ptr->hintSize ) )
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
        this->hintType = field_ptr->hintType;
        this->hintSize = field_ptr->hintSize;
        this->hintData = &this->updateHintData;
        ::memcpy( this->hintData, field_ptr->hintData, this->hintSize );
        break;
    }
  }
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i8 i8 )
{
  this->name          = fname;
  this->nameLen       = fnameLen;
  this->type          = RAIMSG_INT;
  this->hintType      = RAIMSG_NODATA;
  this->updateData.i8 = i8;
  this->data          = &this->updateData.i8;
  this->size          = sizeof( i8 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u8 u8 )
{
  this->name          = fname;
  this->nameLen       = fnameLen;
  this->type          = RAIMSG_UINT;
  this->hintType      = RAIMSG_NODATA;
  this->updateData.u8 = u8;
  this->data          = &this->updateData.u8;
  this->size          = sizeof( u8 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i16 i16 )
{
  Aligned::endianSwap( i16 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_INT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.i16 = i16;
  this->data           = &this->updateData.i16;
  this->size           = sizeof( i16 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u16 u16 )
{
  Aligned::endianSwap( u16 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_UINT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.u16 = u16;
  this->data           = &this->updateData.u16;
  this->size           = sizeof( u16 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i32 i32 )
{
  Aligned::endianSwap( i32 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_INT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.i32 = i32;
  this->data           = &this->updateData.i32;
  this->size           = sizeof( i32 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u32 u32 )
{
  Aligned::endianSwap( u32 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_UINT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.u32 = u32;
  this->data           = &this->updateData.u32;
  this->size           = sizeof( u32 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i64 i64 )
{
  Aligned::endianSwap( i64 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_INT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.i64 = i64;
  this->data           = &this->updateData.i64;
  this->size           = sizeof( i64 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u64 u64 )
{
  Aligned::endianSwap( u64 );
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_UINT;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.u64 = u64;
  this->data           = &this->updateData.u64;
  this->size           = sizeof( u64 );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_f32 f32 )
{
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_REAL;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.f32 = f32;
  this->data           = &this->updateData.f32;
  this->size           = sizeof( f32 );
  Aligned::endianSwap( *(Rai_u32 *) this->data );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_f64 f64 )
{
  this->name           = fname;
  this->nameLen        = fnameLen;
  this->type           = RAIMSG_REAL;
  this->hintType       = RAIMSG_NODATA;
  this->updateData.f64 = f64;
  this->data           = &this->updateData.f64;
  this->size           = sizeof( f64 );
  Aligned::endianSwap( *(Rai_u64 *) this->data );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  const char *str )
{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = RAIMSG_STRING;
  this->hintType = RAIMSG_NODATA;
  this->data     = (char *) str;
  if ( str == NULL )
    this->size = 0;
  else
    this->size = (unsigned int) ::strlen( str ) + 1;
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  RaiMsg *msg_ptr )

{
  RaiMsg_size msgSize;
  Rai_u32     u32;
  Rai_u16     u16;

  if ( msg_ptr != NULL && ! RaiMsg::isValidProto( msg_ptr->proto ) )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );

  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = RAIMSG_MESSAGE;
  this->hintType = RAIMSG_NODATA;
  this->data     = &this->tempMsg;

  if ( msg_ptr == NULL || msg_ptr->msgBuf == NULL ||
         msg_ptr->msgStart + RaiMsgConst::START_OFF[ msg_ptr->proto ] >
           msg_ptr->msgSize ) {
    this->tempMsg.InitSubMessage( NULL, 0, NULL, ( msg_ptr == NULL ) ?
                           RAIMSG_PROTO : msg_ptr->proto, RAIMSG_MEMORY_FIXED );
  }
  else {
    if ( msg_ptr->isDynamic == RAIMSG_MEMORY_FIXED ) {
      this->tempMsg.InitSubMessage( msg_ptr->msgBuf, msg_ptr->msgSize, NULL,
                                    msg_ptr->proto, RAIMSG_MEMORY_FIXED );
    }
    else if ( msg_ptr->msgStart >= RaiMsgConst::HDR_SIZE[ msg_ptr->proto ] ) {
      if ( RaiMsgConst::SIZE_LEN[ msg_ptr->proto ] == 2 ) {
        Unaligned::endianGetInt(
          (Rai_u8 *) &msg_ptr->msgBuf[ msg_ptr->msgStart +
                               RaiMsgConst::SIZE_OFF[ msg_ptr->proto ] ], u16 );
        msgSize = (RaiMsg_size) u16;
      }
      else {
        Unaligned::endianGetInt(
          (Rai_u8 *) &msg_ptr->msgBuf[ msg_ptr->msgStart +
                               RaiMsgConst::SIZE_OFF[ msg_ptr->proto ] ], u32 );
        msgSize = (RaiMsg_size) u32;
      }
      this->tempMsg.InitSubMessage( &msg_ptr->msgBuf[ msg_ptr->msgStart ],
                          msgSize, NULL, msg_ptr->proto, RAIMSG_MEMORY_STATIC );
    }
    else {
      this->tempMsg.InitSubMessage( msg_ptr->msgBuf, msg_ptr->msgSize, NULL,
                                    msg_ptr->proto, RAIMSG_MEMORY_STATIC );
    }
  }
  this->size = this->tempMsg.msgSize;
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  bool b )
{
  this->name               = fname;
  this->nameLen            = fnameLen;
  this->type               = RAIMSG_BOOLEAN;
  this->hintType           = RAIMSG_NODATA;
  this->updateData.boolean = b ? 1 : 0;
  this->data               = &this->updateData.boolean;
  this->size               = sizeof( this->updateData.boolean );
}


void
RaiField::Update( RaiMsg_name fname,  RaiMsg_data partial_data,
                  RaiMsg_size partial_size,  RaiMsg_size offset )
{
  this->UpdateEx( fname, FNAME_LEN( fname ), partial_data, partial_size,
                  offset );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,
                    RaiMsg_data partial_data,  RaiMsg_size partial_size,
                    RaiMsg_size offset )
{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = RAIMSG_PARTIAL;
  this->hintType = RAIMSG_NODATA;
  this->size     = partial_size;
  this->hintSize = offset;
  this->data     = partial_data;
}


void
RaiField::Update( RaiMsg_name fname,  RaiMsg_data array_data,
                  RaiMsg_size num_entries,  RaiMsg_type entry_type,
                  RaiMsg_size entry_size )
{
  this->UpdateEx( fname,  FNAME_LEN( fname ), array_data,
                  num_entries,  entry_type, entry_size );
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,
                    RaiMsg_data array_data,  RaiMsg_size num_entries,
                    RaiMsg_type entry_type,  RaiMsg_size entry_size )

{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = RAIMSG_ARRAY;
  this->hintType = entry_type;
  this->size     = entry_size * num_entries;
  this->data     = array_data;
  this->hintSize = entry_size;

  if ( ! RaiField::isValidArrayType( entry_type, entry_size ) )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
}


void
RaiField::Update( RaiMsg_name fname,  RaiMsg_type ftype,
                  RaiMsg_size fsize,  RaiMsg_data fdata )

{
  this->name     = fname;
  this->nameLen  = FNAME_LEN( fname );
  this->type     = ftype;
  this->size     = fsize;
  this->data     = fdata;
  this->hintType = RAIMSG_NODATA;

  if ( ftype != RAIMSG_STRING && ftype != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( ftype, fsize ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
    this->data = this->AlignData( this->updateData );
  }
}


void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen, RaiMsg_type ftype,
                    RaiMsg_size fsize,  RaiMsg_data fdata )

{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = ftype;
  this->size     = fsize;
  this->data     = fdata;
  this->hintType = RAIMSG_NODATA;

  if ( ftype != RAIMSG_STRING && ftype != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( ftype, fsize ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
    this->data = this->AlignData( this->updateData );
  }
}


void
RaiField::UpdateRaw( RaiMsg_name fname,  RaiMsg_size fnameLen,
                     RaiMsg_type ftype,  RaiMsg_size fsize,
                     RaiMsg_data fdata )
{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = ftype;
  this->size     = fsize;
  this->data     = fdata;
  this->hintType = RAIMSG_NODATA;

  if ( ftype != RAIMSG_STRING && ftype != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( ftype, fsize ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
  }
}


void
RaiField::Update( RaiMsg_name fname,  RaiMsg_type ftype,
                  RaiMsg_size fsize,  RaiMsg_data fdata,
                  RaiMsg_type hint_type,  RaiMsg_size hint_size,
                  RaiMsg_data hint_data )
{
  this->UpdateEx( fname, FNAME_LEN( fname ), ftype, fsize, fdata,
                  hint_type, hint_size, hint_data );
}

void
RaiField::UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,
                    RaiMsg_type ftype,  RaiMsg_size fsize,
                    RaiMsg_data fdata,  RaiMsg_type hint_type,
                    RaiMsg_size hint_size,  RaiMsg_data hint_data )

{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = ftype;
  this->size     = fsize;
  this->data     = fdata;
  this->hintType = hint_type;
  this->hintSize = hint_size;
  this->hintData = hint_data;

  if ( ftype != RAIMSG_STRING && ftype != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( ftype, fsize ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
    this->data = this->AlignData( this->updateData );
  }

  if ( hint_type != RAIMSG_STRING && hint_type != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( hint_type, hint_size ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
    this->hintData = this->AlignHintData( this->updateHintData );
  }
}


void
RaiField::UpdateRaw( RaiMsg_name fname,  RaiMsg_size fnameLen,
                     RaiMsg_type ftype,  RaiMsg_size fsize,  RaiMsg_data fdata,
                     RaiMsg_type hint_type,  RaiMsg_size hint_size,
                     RaiMsg_data hint_data )
{
  this->name     = fname;
  this->nameLen  = fnameLen;
  this->type     = ftype;
  this->size     = fsize;
  this->data     = fdata;
  this->hintType = hint_type;
  this->hintSize = hint_size;
  this->hintData = hint_data;

  if ( ftype != RAIMSG_STRING && ftype != RAIMSG_OPAQUE &&
       ftype != RAIMSG_ARRAY ) {
    if ( ! RaiField::isValidMachineType( ftype, fsize ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
  }

  if ( hint_type != RAIMSG_STRING && hint_type != RAIMSG_OPAQUE ) {
    if ( ! RaiField::isValidMachineType( hint_type, hint_size ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_RAIMSG_MTYPE );
  }
}


bool
RaiField::FindEx( RaiMsg *msg,  RaiMsg_name fname,  RaiMsg_size fnameLen )

{
  RaiMsg_name name;
  RaiMsg_size nameSize;
  Rai_u16     fid,
              myFid;

  if ( fnameLen >= 3 && fname[ fnameLen - 3 ] == '\0' ) {
    Unaligned::endianGetInt( (Rai_u8 *) &fname[ fnameLen - 2 ], fid );

    if ( this->First( msg ) ) {
      do {
        nameSize = this->NameSize();
        name     = this->Name();

        if ( this->Fid( myFid ) ) {
          if ( myFid == fid )
            return true;
          if ( nameSize == fnameLen && ::memcmp( fname, name,
                                                 fnameLen - 2 ) == 0 )
            return true;
        }
        else {
          if ( nameSize == fnameLen - 2 && ::memcmp( fname, name,
                                                     fnameLen - 2 ) == 0 )
            return true;
        }
      } while ( this->Next() );
    }
  }
  else {
    if ( this->First( msg ) ) {
      do {
        nameSize = this->NameSize();
        name     = this->Name();

        if ( nameSize >= 3 && name[ nameSize - 3 ] == '\0' )
          nameSize -= 2;
        if ( nameSize == fnameLen && ::memcmp( fname, name, fnameLen ) == 0 )
          return true;
      } while ( this->Next() );
    }
  }

  return false;
}


bool
RaiField::FindFid( RaiMsg *msg,  Rai_u16 fid )
{
  Rai_u16 myFid;

  if ( this->First( msg ) ) {
    do {
      if ( this->Fid( myFid ) && fid == myFid )
        return true;
    } while ( this->Next() );
  }

  return false;
}


bool
RaiField::Find( RaiMsg *msg,  const RaiMsg_dict *entry )

{
  const RaiMsg_dict * curEntry;
  Rai_u8            * fromPtr,
                    * endPtr;
  RaiMsg_size         foffset;
  Rai_u16             fid;

  if ( ! RaiMsg::isSassProto( msg->proto ) )
    return this->FindEx( msg, (RaiMsg_name) entry->fname,
                         entry->fname_size );
  if ( DataDictionary == NULL )
    throw RaiMsgErr::getErr( RaiMsgErr::NO_DICTIONARY );

  this->fieldStart = msg->msgStart;
  if ( msg->isDynamic != RAIMSG_MEMORY_FIXED )
    this->fieldStart += RaiMsgConst::START_OFF[ msg->proto ];
  this->fieldEnd    = msg->msgStart + msg->SubMsgSize();
  this->iterMsgSize = this->fieldEnd;
  this->iterMsg     = msg;

  try {
    /* if form, know where field is already */
    if ( msg->isForm() ) {
      if ( this->fieldStart + entry->foffset < this->fieldEnd ) {
        foffset = this->fieldStart + entry->foffset;
        fromPtr = &msg->msgBuf[ foffset ];
        Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );

        if ( ( fid & ( MAX_FID - 1 ) ) == entry->fid ) {
          endPtr = entry->unpack( *this, fromPtr, this->fieldEnd - foffset );
          this->fieldEnd   = endPtr - msg->msgBuf;
          this->fieldStart = foffset;
          return true;
        }
      }
      /* entry might not have a foffset */
      if ( entry->foffset != 0 ) {
        this->iterMsg = NULL;
        return false;
      }
    }

    /* search the fid entries */
    while ( this->fieldStart < this->fieldEnd ) {
      fromPtr = &msg->msgBuf[ this->fieldStart ];
      Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );

      if ( ( fid & ( MAX_FID - 1 ) ) == entry->fid ) {
        endPtr = entry->unpack( *this, fromPtr,
                                this->fieldEnd - this->fieldStart );
        this->fieldEnd = endPtr - msg->msgBuf;
        return true;
      }

      curEntry = DataDictionary->getEntry( fid );
      if ( curEntry == NULL ) {
        if ( fid == 0 && this->fieldStart + 2 == this->fieldEnd )
          goto null_dict_fid;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }

      if ( curEntry->partial ) {
        Rai_u16 partialLen;
        Unaligned::endianGetInt( &((Rai_u8 *) fromPtr )[ 4 ], partialLen );
        this->fieldStart += 6 + ( ( partialLen + 1U ) & ~1U );
      }
      else {
        this->fieldStart += curEntry->packSize();
      }
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    throw e;
  }
null_dict_fid:;
  this->iterMsg = NULL;
  return false;
}


bool
RaiField::First( RaiMsg *msg,  const RaiMsg_dict *&entry )

{
  Rai_u8 * fromPtr,
         * endPtr;
  Rai_u16  fid; 

  if ( ! RaiMsg::isSassProto( msg->proto ) ) {
    if ( this->First( msg ) ) {
      if ( DataDictionary != NULL )
        entry = DataDictionary->getEntry( this->Name(), this->NameSize() );
      else
        entry = NULL;
      return true;
    }
    return false;
  }
  try {
    this->fieldStart = msg->msgStart;
    if ( msg->isDynamic != RAIMSG_MEMORY_FIXED )
      this->fieldStart += RaiMsgConst::START_OFF[ msg->proto ];
    this->fieldEnd    = msg->msgStart + msg->SubMsgSize();
    this->iterMsgSize = this->fieldEnd;

    if ( this->fieldStart < this->fieldEnd ) {
      fromPtr = &msg->msgBuf[ this->fieldStart ];

      Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );
      if ( DataDictionary == NULL )
        throw RaiMsgErr::getErr( RaiMsgErr::NO_DICTIONARY );

      entry = DataDictionary->getEntry( fid );
      if ( entry == NULL ) {
        if ( fid == 0 && this->fieldStart + 2 == this->fieldEnd )
          goto null_dict_fid;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }

      this->iterMsg = msg;
      endPtr = entry->unpack( *this, fromPtr,
                              this->fieldEnd - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    throw e;
  }
null_dict_fid:;
  this->iterMsg = NULL;
  return false;
}


bool
RaiField::Next( const RaiMsg_dict *&entry )
{
  RaiMsg * msg;
  Rai_u8 * fromPtr,
         * endPtr;
  Rai_u16  fid; 

  if ( (msg = this->iterMsg) == NULL )
    return false;
  if ( ! RaiMsg::isSassProto( msg->proto ) ) {
    if ( this->Next() ) {
      if ( DataDictionary != NULL )
        entry = DataDictionary->getEntry( this->Name(), this->NameSize() );
      else
        entry = NULL;
      return true;
    }
    return false;
  }
  try {
    this->fieldStart = this->fieldEnd;
    this->fieldEnd   = this->iterMsgSize;

    if ( this->fieldStart < this->fieldEnd ) {
      fromPtr = &msg->msgBuf[ this->fieldStart ];

      Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );
      entry = DataDictionary->getEntry( fid );

      if ( entry == NULL ) {
        if ( fid == 0 && this->fieldStart + 2 == this->fieldEnd )
          goto null_dict_fid;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }

      endPtr = entry->unpack( *this, fromPtr,
                              this->fieldEnd - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    throw e;
  }
null_dict_fid:;
  this->iterMsg = NULL;
  return false;
}


bool
RaiField::First( RaiMsg *msg,  const RaiMsg_form *form,
                 const RaiMsg_dict *&entry )
{
  Rai_u8 * fromPtr,
         * endPtr;
  Rai_u16  fid; 

  try {
    if ( ! RaiMsg::isSassProto( msg->proto ) )
      throw RaiMsgErr::getErr( RaiMsgErr::NOT_SASS_FORM );

    this->fieldStart = msg->msgStart;
    if ( msg->isDynamic != RAIMSG_MEMORY_FIXED )
      this->fieldStart += RaiMsgConst::START_OFF[ msg->proto ];
    this->fieldEnd    = msg->msgStart + msg->SubMsgSize();
    this->iterMsgSize = this->fieldEnd;

    if ( this->fieldStart < this->fieldEnd ) {
      fromPtr = &msg->msgBuf[ this->fieldStart ];

      Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );
      entry = form->getEntry( fid );
      if ( entry == NULL ) {
        if ( fid == 0 && this->fieldStart + 2 == this->fieldEnd )
          goto null_dict_fid;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }

      this->iterMsg = msg;
      endPtr = entry->unpack( *this, fromPtr,
                              this->fieldEnd - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    throw e;
  }
null_dict_fid:;
  this->iterMsg = NULL;
  return false;
}


bool
RaiField::Next( const RaiMsg_form *form,
                const RaiMsg_dict *&entry )
{
  RaiMsg * msg;
  Rai_u8 * fromPtr,
         * endPtr;
  Rai_u16  fid; 

  if ( (msg = this->iterMsg) == NULL )
    return false;

  try {
    this->fieldStart = this->fieldEnd;
    this->fieldEnd   = this->iterMsgSize;

    if ( this->fieldStart < this->fieldEnd ) {
      fromPtr = &msg->msgBuf[ this->fieldStart ];

      Unaligned::endianGetInt( (Rai_u8 *) fromPtr, fid );
      entry = form->getEntry( fid );
      if ( entry == NULL ) {
        if ( fid == 0 && this->fieldStart + 2 == this->fieldEnd )
          goto null_dict_fid;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_DICT_FID );
      }

      endPtr = entry->unpack( *this, fromPtr,
                              this->fieldEnd - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    throw e;
  }
null_dict_fid:;
  this->iterMsg = NULL;
  return false;
}


bool
RaiField::First( RaiMsg *msg )
{
  Rai_u8 * endPtr;

  try {
    if ( ! RaiMsg::isValidProto( msg->proto ) )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_PROTO );

    this->fieldStart = msg->msgStart;
    if ( msg->isDynamic != RAIMSG_MEMORY_FIXED )
      this->fieldStart += RaiMsgConst::START_OFF[ msg->proto ];
    this->fieldEnd    = msg->msgStart + msg->SubMsgSize();
    this->iterMsgSize = this->fieldEnd;

    if ( this->fieldStart < this->fieldEnd ) {
      this->iterMsg = msg;
      endPtr = this->UnPack( msg->proto, &msg->msgBuf[ this->fieldStart ],
                             this->fieldEnd - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    if ( e != RaiMsgErr::getErr( RaiMsgErr::NULL_DICT_FID ) )
      throw e;
  }

  this->iterMsg = NULL;
  return false;
}


bool
RaiField::Next( void )
{
  RaiMsg * msg;
  Rai_u8 * endPtr;

  if ( (msg = this->iterMsg) == NULL )
    return false;

  try {
    this->fieldStart = this->fieldEnd;

    if ( this->fieldStart < this->iterMsgSize ) {
      endPtr = this->UnPack( msg->proto, &msg->msgBuf[ this->fieldStart ],
                             this->iterMsgSize - this->fieldStart );
      this->fieldEnd = endPtr - msg->msgBuf;
      return true;
    }
  } catch ( Error e ) {
    this->iterMsg = NULL;
    if ( e != RaiMsgErr::getErr( RaiMsgErr::NULL_DICT_FID ) )
      throw e;
  }

  this->iterMsg = NULL;
  return false;
}


void
RaiField::SetPointer( RaiMsg *msg,  RaiMsg_size fieldStart )

{
  Rai_u8 * endPtr;

  this->iterMsg     = msg;
  this->fieldStart  = fieldStart;
  this->fieldEnd    = msg->msgStart + msg->SubMsgSize();
  this->iterMsgSize = this->fieldEnd;
  endPtr = this->UnPack( msg->proto, &msg->msgBuf[ fieldStart ],
                         this->fieldEnd - fieldStart );
  this->fieldEnd = endPtr - msg->msgBuf;
}


#if 0
bool
RaiField::FirstRaw( RaiMsg *msg )
{
  return this->First( msg );
}


bool
RaiField::NextRaw( void )
{
  return this->Next();
}
#endif

#if 0
RaiMsg_data
RaiField::RawData( RaiMsg_size *dataLen )
{
  if ( this->iterMsg == NULL )
    throw RaiMsgErr::getErr( RaiMsgErr::NOT_FOUND );
  if ( dataLen != NULL )
    *dataLen = this->fieldEnd - this->fieldStart;
  return &this->iterMsg->msgBuf[ this->fieldStart ];
}
#endif

#if 0
RaiMsg_data
RaiField::RawHintData( void )
{
  RaiMsg_size hintSize;

  if ( this->iterMsg == NULL || this->type == RAIMSG_ARRAY ||
       this->type == RAIMSG_PARTIAL || this->hintType == RAIMSG_NODATA )
    throw RaiMsgErr::getErr( RaiMsgErr::NOT_FOUND );

  hintSize = this->hintSize;
  if ( hintSize > 0xff )
    hintSize += 1 + 4;
  else
    hintSize += 1 + 1;
  return &this->iterMsg->msgBuf[ this->fieldEnd - hintSize ] ;
}
#endif


void
RaiField::Print( OutputStream *output_file,  Rai_u32 field_newlines,
                 const char *fname_format,  Rai_u32 print_opaques,
                 const char *debug_format,  const char *debug_hformat )

{
  RaiField_data  val;
  RaiMsg_data    ar;
  const char   * ptr;
  char           buf[ 300 ];
  unsigned short fid;
  unsigned int   i,
                 j;

  if ( field_newlines > 1 )
    output_file->printf( "%*s", field_newlines - 1, "" );
  if ( this->name != NULL /*&& this->iterMsg != NULL*/ ) {
    for ( i = 0, ptr = this->name; i < 256; ) {
      if ( (buf[ i++ ] = *ptr++) == '\0' )
        break;
    }
    if ( i + 2 == this->nameLen ) {
      Unaligned::endianGetInt( (Rai_u8 *) ptr, fid );
      buf[ i - 1 ] = '[';
      j = i + 8;
      const bool isNeg = (short) fid < 0;
      if ( isNeg )
        fid = (unsigned short) -( (short) fid );
      do {
        buf[ --j ] = ( fid % 10 ) + '0';
        fid /= 10;
      } while ( fid != 0 );
      if ( isNeg )
        buf[ --j ] = '-';
      ::memmove( &buf[ i ], &buf[ j ], i + 8 - j );
      i += i + 8 - j;
      buf[ i ] = ']';
      buf[ i + 1 ] = '\0';
    }
    output_file->printf( ( fname_format == NULL ? "%s=" : fname_format ), buf );
  }
  else {
    output_file->printf( ( fname_format == NULL ? "%s=" : fname_format ),
                         ( this->name == NULL ? "(null)" : this->name ) );
  }

  if ( debug_format != NULL )
    output_file->printf( debug_format, RaiMsg::TypeStr( this->type ),
                         (int) this->size );
  if ( this->data == NULL )
    output_file->puts( "(null)" );
  else {
    switch ( this->type ) {
      case RAIMSG_MESSAGE:
        output_file->puts( "{" );
        if ( field_newlines ) {
          output_file->puts( "\n" );
          field_newlines += 4;
        }
        else {
          output_file->puts( " " );
        }
        ((RaiMsg *) this->data)->Print( output_file, field_newlines,
                                        fname_format, print_opaques,
                                        debug_format, debug_hformat );
        if ( field_newlines > 5 )
          output_file->printf( "%*s", field_newlines - 5, "" );
        output_file->puts( "}" );
        break;
      case RAIMSG_OPAQUE:
        if ( this->iterMsg != NULL ) {
          if ( this->iterMsg->proto == RV_PROTO ) {
            /* check if subject */
            if ( this->hintType == RAIMSG_UINT &&
                 this->hintSize == 1 &&
                 ((byte *) this->hintData)[ 0 ] == RAI_RV_SUBJECT ) {
              RvSubject subj;
              char      sbuf[ SassConst::MAX_SUBJECT_LEN ];
              subj.set( (byte *) this->data, this->size );
              output_file->puts( subj.toString( sbuf, sizeof( sbuf ) ) );
              break;
            }
          }
        }
      /* FALLTHRU */
      case RAIMSG_PARTIAL:
        if ( print_opaques ) {
      case RAIMSG_STRING:
          output_file->puts( "\"" );
          for ( ptr = (const char *) this->data, i = 0;
                ptr < &((const char *) this->data)[ this->size ]; ptr++ ) {
            if ( *ptr == '\0' && this->type == RAIMSG_STRING )
              break;
            if ( (Rai_u8) *ptr >= ' ' && (Rai_u8) *ptr <= 127 )
              buf[ i ] = *ptr;
            else
              buf[ i ] = '.';
            if ( ++i == sizeof( buf ) ) {
              output_file->writeBytes( (Rai_u8 *) buf, sizeof( buf ) );
              i = 0;
            }
          }
          if ( i > 0 )
            output_file->writeBytes( (Rai_u8 *) buf, i );
          output_file->puts( "\"" );
          if ( this->type == RAIMSG_PARTIAL )
            output_file->printf( " <offset=%u>", this->hintSize );
        }
        else {
          output_file->puts( "(opaque)" );
        }
        break;
      case RAIMSG_ARRAY:
        output_file->puts( "[" );
        for ( ar = this->data;
              (const char *) ar < &((const char *) this->data)[ this->size ];
              ar = (RaiMsg_data) &((const char *) ar)[ this->hintSize ] ) {
          if ( ar != this->data )
            output_file->puts( " " );
          if ( this->hintType == RAIMSG_STRING ) {
            output_file->printf( "\"%.*s\"", this->hintSize, (char *) ar );
          }
          else if ( this->hintType == RAIMSG_OPAQUE ) {
            if ( print_opaques ) {
              output_file->puts( "\"" );
              for ( ptr = (const char *) ar, i = 0;
                    ptr < &((const char *) ar)[ this->hintSize ]; ptr++ ) {
                if ( (Rai_u8) *ptr >= ' ' && (Rai_u8) *ptr <= 127 )
                  buf[ i ] = *ptr;
                else
                  buf[ i ] = '.';
                if ( ++i == sizeof( buf ) ) {
                  output_file->writeBytes( (Rai_u8 *) buf, sizeof( buf ) );
                  i = 0;
                }
              }
              if ( i > 0 )
                output_file->writeBytes( (Rai_u8 *) buf, i );
              output_file->puts( "\"" );
            }
            else {
              output_file->puts( "(opaque)" );
            }
          }
          else {
            switch ( this->hintSize ) {
              case 1:
                val.u8 = *(Rai_u8 *) ar;
                break;
              case 2:
                ::memcpy( &val.u16, ar, 2 );
                Aligned::endianSwap( val.u16 );
                break;
              case 4:
                ::memcpy( &val.u32, ar, 4 );
                if ( this->hintType != RAIMSG_IPDATA )
                  Aligned::endianSwap( val.u32 );
                break;
              case 8:
                ::memcpy( &val.u64, ar, 8 );
                Aligned::endianSwap( val.u64 );
                break;
            }
            RaiField::Convert( RAIMSG_STRING, sizeof( buf ), (RaiMsg_data) buf, 
                               this->hintType, this->hintSize,
                               (RaiMsg_data) &val );
            output_file->puts( buf );
          }
        }
        output_file->puts( "]" );
        break;
      case RAIMSG_BOOLEAN:
      case RAIMSG_REAL:
      case RAIMSG_IPDATA:
        this->Convert( buf, sizeof( buf ) );
        output_file->puts( buf );
        break;
      case RAIMSG_INT:
      case RAIMSG_UINT: {
        this->Convert( buf, sizeof( buf ) );
        output_file->puts( buf );

        static const char MSG_TYPE_STR[]   = "MSG_TYPE",
                          REC_TYPE_STR[]   = "REC_TYPE",
                          REC_STATUS_STR[] = "REC_STATUS";
        if ( this->nameLen >= sizeof( MSG_TYPE_STR ) &&
             this->name[ 3 ] == '_' &&
             ( this->name[ 0 ] == 'M' || this->name[ 0 ] == 'R' ) ) {
          const char * s;
          char         buf[ 24 ];
          Rai_u16      xType;

          if ( ::memcmp( this->name, MSG_TYPE_STR,
                         sizeof( MSG_TYPE_STR ) ) == 0 ) {
            this->Convert( xType );
            if ( (s = SassConst::msgTypeToString( xType, buf )) != buf )
              output_file->printf( "  [%s]", s );
          }
          else if ( DataDictionary != NULL &&
                    ::memcmp( this->name, REC_TYPE_STR,
                              sizeof( REC_TYPE_STR ) ) == 0 ) {
            this->Convert( xType );
            const RaiMsg_form *f = DataDictionary->getForm( xType );
            if ( f != NULL )
              output_file->printf( "  [%s]", f->entry->fname );
          }
          else if ( ::memcmp( this->name, REC_STATUS_STR,
                              sizeof( REC_STATUS_STR ) ) == 0 ) {
            this->Convert( xType );
            if ( (s = SassConst::recStatusToString( xType, buf )) != buf )
              output_file->printf( "  [%s]", s );
          }
        }
        break;
      }
      default:
        break;
    }
  }
  if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_OPAQUE &&
       this->hintType != RAIMSG_NODATA ) {
    if ( this->hintData == NULL )
      output_file->puts( " <null>" );
    else {
      this->HintConvert( buf, sizeof( buf ) );
      output_file->printf( " <%s>", buf );
    }
  }
  if ( field_newlines )
    output_file->puts( "\n" );
  else
    output_file->puts( " " );
}


static inline void
writeXmlData( OutputStream *output_file,  const char *data, unsigned int len )
{
  const char * ptr;
  char         buf[ 6 * 1024 ];
  unsigned int i, j;
  bool         binary = false;

  while ( len > 0 && data[ len - 1 ] == 0 ) /* trim zeroes */
    len--;
  for ( i = 0; i < len; i++ ) {
    if ( ( data[ i ] & 0x80 ) != 0 ||
         ( data[ i ] < ' ' && data[ i ] != '\n' &&
           data[ i ] != '\r' && data[ i ] != '\t' ) ) {
      binary = true;
      break;
    }
  }
  if ( ! binary ) {
    for ( ptr = data; ptr < &data[ len ]; ptr = &ptr[ j ] ) {
      j = &data[ len ] - ptr;
      if ( j > sizeof( buf ) / 6 )
        j = sizeof( buf ) / 6;
      i = StrUtil::escapeXmlBuf( buf, ptr, j );
      output_file->writeBytes( (Rai_u8 *) buf, i );
    }
  }
  else {
    char *tmp = buf;
    if ( len * 4 / 3 + 4 > sizeof( buf ) )
      MALLOC( len * 4 / 3 + 4, &tmp );
    i = StrUtil::base64encode( data, len, tmp );
    output_file->printf( "<base64 len=\"%u\">", len );
    output_file->writeBytes( (byte *) tmp, i );
    output_file->puts( "</base64>" );
    if ( tmp != buf )
      FREE( tmp );
  }
}


void
RaiField::PrintXML( OutputStream *output_file,  Rai_u32 attr_flags,
                    Rai_u32 field_newlines )
{
  RaiField_data  val;
  RaiMsg_data    ar;
  const char   * namePtr;
  char           nameBuf[ 300 ],
                 buf[ 6 * 1024 ],
               * p;
  unsigned short fid;
  unsigned int   i,
                 j,
                 mangle;

  fid = 0xffffU;
  mangle = 0;
  if ( this->name != NULL ) {
    if ( this->name[ 0 ] == '\0' ) {
      if ( this->nameLen == 3 ) {
        Unaligned::endianGetInt( (const Rai_u8 *) &this->name[ 1 ], fid );
        i = fid;
        j = 64;
        nameBuf[ --j ] = '\0';
        do {
          nameBuf[ --j ] = ( i % 10 ) + '0';
          i /= 10;
        } while ( i != 0 );
        nameBuf[ --j ] = '-';
        nameBuf[ --j ] = 'd';
        nameBuf[ --j ] = 'i';
        nameBuf[ --j ] = 'f';
        namePtr = &nameBuf[ j ];
      }
      else {
        namePtr = "fid-0";
      }
    }
    else {
      if ( ( attr_flags & RaiMsg::ADD_FID_ATTR ) != 0 ) {
        i = (unsigned int) ::strlen( this->name );
        if ( i + 3 == this->nameLen )
          Unaligned::endianGetInt( (const Rai_u8 *) &this->name[ i + 1 ], fid );
      }
      p = nameBuf;
      for ( namePtr = this->name; ; ) {
        if ( ( *namePtr >= 'A' && *namePtr <= 'Z' ) ||
             ( *namePtr >= 'a' && *namePtr <= 'z' ) || *namePtr == '_' )
          *p++ = *namePtr;
        else if ( ( *namePtr >= '0' && *namePtr <= '9' ) ||
                  *namePtr == '-' || *namePtr == '.' || *namePtr == ':' ) {
          if ( p == nameBuf ) {
            *p++ = '_';
            mangle |= 1;
          }
          *p++ = *namePtr;
        }
        else if ( *namePtr == ' ' ) {
          *p++ = '-';
          mangle |= 2;
        }
        else if ( *namePtr == ',' ) {
          *p++ = '_';
          mangle |= 4;
        }
        else if ( *namePtr == ';' ) {
          *p++ = '_';
          mangle |= 8;
        }
        if ( *++namePtr == '\0' ) {
          if ( p == nameBuf ) {
            namePtr = "mt";
            mangle |= 16;
          }
          else {
            *p = '\0';
            namePtr = nameBuf;
          }
          break;
        }
      }
    }
  }
  else {
    namePtr = "null";
  }

  if ( attr_flags == 0 ) {
    if ( mangle == 0 )
      output_file->printf( "%*s<%s>",
                           ( field_newlines <= 1 ? 0 : field_newlines - 1 ),
                           "", namePtr );
    else
      output_file->printf( "%*s<%s nam=\"%u\">",
                           ( field_newlines <= 1 ? 0 : field_newlines - 1 ),
                           "", namePtr, mangle );
  }
  else {
    output_file->printf( "%*s<%s",
                         ( field_newlines <= 1 ? 0 : field_newlines - 1 ),
                         "", namePtr );
    if ( ( attr_flags & RaiMsg::ADD_TYPE_ATTR ) != 0 ) {
      output_file->printf( " typ=\"%s\"", RaiMsg::TypeStr( this->type ) );
    }
    if ( ( attr_flags & RaiMsg::ADD_SIZE_ATTR ) != 0 ) {
      output_file->printf( " siz=\"%u\"", this->size );
    }
    if ( fid != 0xffffU && ( attr_flags & RaiMsg::ADD_FID_ATTR ) != 0 ) {
      output_file->printf( " fid=\"%u\"", (unsigned int) fid );
    }
    if ( mangle != 0 ) {
      output_file->printf( " nam=\"%u\"", mangle );
    }
    if ( this->type == RAIMSG_PARTIAL ) {
      if ( ( attr_flags & RaiMsg::ADD_PARTIAL_OFF_ATTR ) != 0 )
        output_file->printf( " off=\"%u\"", this->hintSize );
    }
    else if ( this->type == RAIMSG_ARRAY ) {
      if ( ( attr_flags & RaiMsg::ADD_ARRAY_COUNT_ATTR ) != 0 )
        output_file->printf( " cnt=\"%u\"", this->size / this->hintSize );
      if ( ( attr_flags & RaiMsg::ADD_ARRAY_ELSIZE_ATTR ) != 0 )
        output_file->printf( " asz=\"%u\"", this->hintSize );
      if ( ( attr_flags & RaiMsg::ADD_ARRAY_TYPE_ATTR ) != 0 )
        output_file->printf( " atp=\"%s\"", RaiMsg::TypeStr( this->hintType ) );
    }
    else if ( this->hintType != RAIMSG_NODATA ) {
      if ( ( attr_flags & RaiMsg::ADD_HINT_TYPE_ATTR ) != 0 )
        output_file->printf( " htp=\"%s\"", RaiMsg::TypeStr( this->hintType ) );
      if ( ( attr_flags & RaiMsg::ADD_HINT_SIZE_ATTR ) != 0 )
        output_file->printf( " hsz=\"%u\"", this->hintSize );
      if ( ( attr_flags & RaiMsg::ADD_HINT_ATTR ) != 0 ) {
        if ( this->hintData == NULL )
          ::strcpy( buf, "(null)" );
        else
          this->HintConvert( buf, sizeof( buf ) );
        output_file->puts( " hnt=\"" );
        writeXmlData( output_file, buf, (unsigned int) ::strlen( buf ) );
        output_file->puts( "\"" );
      }
    }
    output_file->puts( ">" );
  }

  if ( this->data == NULL )
    output_file->puts( "(null)" );
  else {
    switch ( this->type ) {
      case RAIMSG_MESSAGE:
        if ( field_newlines != 0 )
          output_file->puts( "\n" );
        ((RaiMsg *) this->data)->PrintXML( output_file, attr_flags,
                                           field_newlines, NULL, NULL );
        if ( field_newlines > 1 )
          output_file->printf( "%*s", field_newlines - 1, "" );
        break;
      case RAIMSG_OPAQUE:
      case RAIMSG_PARTIAL:
      case RAIMSG_STRING:
        writeXmlData( output_file, (const char *) this->data, this->size );
        break;
      case RAIMSG_ARRAY:
        for ( ar = this->data;
              (const char *) ar < &((const char *) this->data)[ this->size ];
              ar = (RaiMsg_data) &((const char *) ar)[ this->hintSize ] ) {
          output_file->puts( "<el>" );
          if ( this->hintType == RAIMSG_STRING ||
               this->hintType == RAIMSG_OPAQUE ) {
            writeXmlData( output_file, (const char *) ar, this->hintSize );
          }
          else {
            switch ( this->hintSize ) {
              case 1:
                val.u8 = *(Rai_u8 *) ar;
                break;
              case 2:
                ::memcpy( &val.u16, ar, 2 );
                Aligned::endianSwap( val.u16 );
                break;
              case 4:
                ::memcpy( &val.u32, ar, 4 );
                if ( this->hintType != RAIMSG_IPDATA )
                  Aligned::endianSwap( val.u32 );
                break;
              case 8:
                ::memcpy( &val.u64, ar, 8 );
                Aligned::endianSwap( val.u64 );
                break;
            }
            RaiField::Convert( RAIMSG_STRING, sizeof( buf ), (RaiMsg_data) buf, 
                               this->hintType, this->hintSize,
                               (RaiMsg_data) &val );
            output_file->puts( buf );
          }
          output_file->puts( "</el>" );
        }
        break;
      case RAIMSG_BOOLEAN:
      case RAIMSG_INT:
      case RAIMSG_UINT:
      case RAIMSG_REAL:
      case RAIMSG_IPDATA:
        this->Convert( buf, sizeof( buf ) );
        output_file->puts( buf );
        break;
      default:
        break;
    }
  }
  output_file->printf( "</%s>%s", namePtr,
                       ( field_newlines == 0 ? "" : "\n" ) );
}

unsigned int RaiField::cvtFloatToStringPrecision,
             RaiField::overrideFloatToStringPrecision;

static bool
is_mktfeed_blob( const byte *fptr,  RaiMsg_size fsize,  RaiMsg_data dest,
                 RaiMsg_size dsize )
{
  Rai_u64 ival = 0;
  for ( unsigned int i = 0; i < fsize; i++ ) {
    if ( fptr[ i ] < 0x40 || fptr[ i ] >= 0x80 ) {
      if ( fptr[ i ] == 0 )
        break;
      return false;
    }
    ival += ( fptr[ i ] & 0x3f ) << ( 6 * i );
  }
  if ( dsize == 4 )
    *(Rai_u32 *) dest = (Rai_u32) ival;
  else if ( dsize == 2 )
    *(Rai_u16 *) dest = (Rai_u16) ival;
  else if ( dsize == 1 )
    *(Rai_u8 *) dest = (Rai_u8) ival;
  else
    *(Rai_u64 *) dest = ival;
  return true;
}


static unsigned int
realToStringPrecision( unsigned int hint )
{
  unsigned int prec;
  if ( RaiField::cvtFloatToStringPrecision == 0 ) {
    const char *p;
    RaiField::cvtFloatToStringPrecision = StrUtil::UNTIL_ZERO;
    RaiField::overrideFloatToStringPrecision = 0;
    if ( (p = ::getenv( "RAIMSG_CVT_PRECISION" )) != NULL ) {
      bool dot = false;
      unsigned int a = 0, b = 0;
      for ( const char *q = p; *q != '\0'; q++ ) {
        if ( *q == '.' ) {
          dot = true;
        }
        else if ( *q >= '0' && *q <= '9' ) {
          if ( dot )
            b = ( b * 10 ) + ( *q - '0' );
          else
            a = ( a * 10 ) + ( *q - '0' );
        }
      }
      if ( a != 0 )
        RaiField::cvtFloatToStringPrecision = a;
      if ( b != 0 )
        RaiField::overrideFloatToStringPrecision = b;
    }
  }
  if ( RaiField::overrideFloatToStringPrecision != 0 )
    return RaiField::overrideFloatToStringPrecision;
  prec = RaiField::cvtFloatToStringPrecision;
  switch ( hint ) {
    case RAI_TSS_HINT_DENOM_2: prec = 1; break;
    case RAI_TSS_HINT_DENOM_4: prec = 2; break;
    case RAI_TSS_HINT_DENOM_8: prec = 3; break;
    case RAI_TSS_HINT_DENOM_16: prec = 4; break;
    case RAI_TSS_HINT_DENOM_32: prec = 5; break;
    case RAI_TSS_HINT_DENOM_64: prec = 6; break;
    case RAI_TSS_HINT_DENOM_128: prec = 7; break;
    case RAI_TSS_HINT_DENOM_256: prec = 8; break;
    case RAI_TSS_HINT_PRECISION_1: prec = 1; break;
    case RAI_TSS_HINT_PRECISION_2: prec = 2; break;
    case RAI_TSS_HINT_PRECISION_3: prec = 3; break;
    case RAI_TSS_HINT_PRECISION_4: prec = 4; break;
    case RAI_TSS_HINT_PRECISION_5: prec = 5; break;
    case RAI_TSS_HINT_PRECISION_6: prec = 6; break;
    case RAI_TSS_HINT_PRECISION_7: prec = 7; break;
    case RAI_TSS_HINT_PRECISION_8: prec = 8; break;
    case RAI_TSS_HINT_PRECISION_9: prec = 9; break;
    case RAI_TSS_HINT_PRECISION_9+1: prec = 10; break;
    case RAI_TSS_HINT_PRECISION_9+2: prec = 11; break;
    case RAI_TSS_HINT_PRECISION_9+3: prec = 12; break;
    case RAI_TSS_HINT_PRECISION_9+4: prec = 13; break;
    case RAI_TSS_HINT_PRECISION_9+5: prec = 14; break;
    case RAI_TSS_HINT_PRECISION_9+6: prec = 15; break;
    default: break;
  }
  return prec;
}


void
RaiField::Convert( ConvertCtx &ctx )
{
  RaiMsg_type dtype = ctx.destType;
  RaiMsg_size dsize = ctx.destSize;
  RaiMsg_data dest  = ctx.destData;
  //Rai_u16     dhint = ctx.destHint;

  RaiMsg_type stype = ctx.srcType;
  RaiMsg_size ssize = ctx.srcSize;
  RaiMsg_data src   = ctx.srcData;
  Rai_u16     shint = ctx.srcHint;

  Rai_i8       i8;
  Rai_u8       u8;
  Rai_i16      i16;
  Rai_u16      u16;
  Rai_u32      u32;
  Rai_i32      i32;
  Rai_u64      u64;
  Rai_i64      i64;
  Rai_f32      f32;
  Rai_f64      f64;
  unsigned int i,
               a[ 4 ];
  char       * quad,
             * qend,
             * str,
             * end;

  switch ( stype ) {
    case RAIMSG_STRING:
    case RAIMSG_OPAQUE:
    case RAIMSG_PARTIAL:
      if ( dsize == 0 || dest == NULL ) {
        if ( ssize == 0 || src == NULL )
          return;
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_STRING );
      }
      if ( ssize == 0 || src == NULL ) {
        if ( dtype == RAIMSG_STRING )
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_STRING );
        src   = (RaiMsg_data) "";
        ssize = 1;
      }
      switch ( dtype ) {
        case RAIMSG_STRING:
          if ( dsize > ssize ) {
            ::strncpy( (char *) dest, (char *) src, ssize );
            ((char *) dest)[ ssize ] = '\0';
          }
          else {
            ::strncpy( (char *) dest, (char *) src, dsize );
          }
          break;
        case RAIMSG_BOOLEAN: {
          if ( dsize != 1 )
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_BOOL );
          if ( *(char *) src == '\0' )
            *(Rai_u8 *) dest = 0;
          else {
            char tmpBool[ 8 ];
            if ( ssize < sizeof( tmpBool ) - 1 ) {
              ::memcpy( tmpBool, src, ssize );
              tmpBool[ ssize ] = '\0';
              if ( StrUtil::parseBoolean( tmpBool ) )
                *(Rai_u8 *) dest = 1;
              else
                *(Rai_u8 *) dest = 0;
            }
            else {
              if ( StrUtil::parseBoolean( (char *) src ) )
                *(Rai_u8 *) dest = 1;
              else
                *(Rai_u8 *) dest = 0;
            }
          }
          break;
        }
        case RAIMSG_INT: {
          str = (char *) src;
          end = &str[ ssize ];
          while ( str < end && ( *str == ' ' || *str == '+' ) )
            str++;
          if ( dsize == 4 )
            *(Rai_i32 *) dest = (Rai_i32) ::strtol( str, &end, 0 );
          else if ( dsize == 2 )
            *(Rai_i16 *) dest = (Rai_i16) ::strtol( str, &end, 0 );
          else if ( dsize == 1 )
            *(Rai_i8 *) dest = (Rai_i8) ::strtol( str, &end, 0 );
          else if ( dsize == 8 ) {
            if ( is_mktfeed_blob( (byte *) (void *) str, ssize, dest, dsize ) )
              break;
            StrUtil::parseInt( str, (Rai_i64 *) dest, (const char **) &end );
          }
          else
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
          if ( str == end && *str != '\0' ) {
            if ( is_mktfeed_blob( (byte *) (void *) str, ssize, dest, dsize ) )
              break;
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
          }
          break;
        }
        case RAIMSG_UINT: {
          str = (char *) src;
          end = &str[ ssize ];
          while ( str < end && ( *str == ' ' || *str == '+' ) )
            str++;
          if ( dsize == 4 )
            *(Rai_u32 *) dest = (Rai_u32) ::strtoul( str, &end, 0 );
          else if ( dsize == 2 )
            *(Rai_u16 *) dest = (Rai_u16) ::strtoul( str, &end, 0 );
          else if ( dsize == 1 )
            *(Rai_u8 *) dest = (Rai_u8) ::strtoul( str, &end, 0 );
          else if ( dsize == 8 ) {
            if ( is_mktfeed_blob( (byte *) (void *) str, ssize, dest, dsize ) )
              break;
            StrUtil::parseInt( str, (Rai_u64 *) dest, (const char **) &end );
          }
          else
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
          if ( str == end && *str != '\0' ) {
            if ( is_mktfeed_blob( (byte *) (void *) str, ssize, dest, dsize ) )
              break;
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
          }
          break;
        }
        case RAIMSG_REAL: {
          unsigned int precision, denom;
          bool novalue;
          end = &((char *) src)[ ssize ];
          if ( *(char *) src != '\0' ) {
            if ( dsize == 8 )
              StrUtil::parseFloat2( (char *) src, (Rai_f64 *) dest,
                                    (const char **) &end,
                                    U_FRACTION | U_PERCENT,
                                    precision, denom, novalue );
            else if ( dsize == 4 )
              StrUtil::parseFloat2( (char *) src, (Rai_f32 *) dest,
                                    (const char **) &end,
                                    U_FRACTION | U_PERCENT,
                                    precision, denom, novalue );
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            if ( novalue )
              ctx.destHint = RAI_TSS_HINT_BLANK_VALUE;
            else {
              if ( precision == 0 ) {
                switch ( denom ) {
                  default:
                  case 0: break;
                  case 2: ctx.destHint = RAI_TSS_HINT_DENOM_2; break;
                  case 4: ctx.destHint = RAI_TSS_HINT_DENOM_4; break;
                  case 8: ctx.destHint = RAI_TSS_HINT_DENOM_8; break;
                  case 16: ctx.destHint = RAI_TSS_HINT_DENOM_16; break;
                  case 32: ctx.destHint = RAI_TSS_HINT_DENOM_32; break;
                  case 64: ctx.destHint = RAI_TSS_HINT_DENOM_64; break;
                  case 128: ctx.destHint = RAI_TSS_HINT_DENOM_128; break;
                  case 256: ctx.destHint = RAI_TSS_HINT_DENOM_256; break;
                }
              }
              else {
                if ( precision < 16 ) /* 17 -> 31 */
                  ctx.destHint = 16 + precision;
                else
                  ctx.destHint = 31;
              }
            }
          }
          else {
            if ( dsize == 8 )
              *(Rai_f64 *) dest = 0.0;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = 0.0;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            ctx.destHint = RAI_TSS_HINT_BLANK_VALUE;
          }
          break;
        }
        case RAIMSG_IPDATA:
          end = &((char *) src)[ ssize ];
          if ( dsize == 4 ) {
            quad = (char *) src;
            qend = end;
            for ( i = 0; i < 4; i++ ) {
              a[ i ] = ::strtoul( quad, &qend, 0 );
              if ( qend == quad )
                break;
              if ( i < 3 ) {
                if ( &qend[ 1 ] >= end || *qend != '.' )
                  break;
                quad = &qend[ 1 ];
              }
            }
            if ( i != 4 || a[ 0 ] > 255 || a[ 1 ] > 255 ||
                           a[ 2 ] > 255 || a[ 3 ] > 255 ) {
              u32 = (Rai_u32) ::strtoul( (char *) src, &end, 0 );
              if ( (char *) src == end && *(char *) src != '\0' )
                throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_IPDATA );
              a[ 0 ] = u32 >> 24;           a[ 1 ] = ( u32 >> 16 ) & 0xff;
              a[ 2 ] = ( u32 >> 8 ) & 0xff; a[ 3 ] = u32 & 0xff;
            }
            ((Rai_u8 *) dest)[ 0 ] = (Rai_u8) a[ 0 ];
            ((Rai_u8 *) dest)[ 1 ] = (Rai_u8) a[ 1 ];
            ((Rai_u8 *) dest)[ 2 ] = (Rai_u8) a[ 2 ];
            ((Rai_u8 *) dest)[ 3 ] = (Rai_u8) a[ 3 ];
          }
          else if ( dsize == 2 ) {
            *(Rai_u16 *) dest = (Rai_u16) ::strtoul( (char *) src, &end, 0 );
            if ( (char *) src == end && *(char *) src != '\0' )
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_IPDATA );
          }
          else
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_IPDATA );
          break;
        case RAIMSG_OPAQUE:
        case RAIMSG_PARTIAL:
          if ( dsize > ssize )
            ::memcpy( dest, src, ssize );
          else
            ::memcpy( dest, src, dsize );
          break;
        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_STRING );
      }
      break;
    case RAIMSG_IPDATA:
      switch ( dtype ) {
        case RAIMSG_STRING:
          if ( ssize == 4 ) {
            quad = (char *) dest;
            StrUtil::intToString( ((Rai_u8 *) src)[ 0 ], quad, dsize,
                                  U_DECIMAL, false, &quad );
            *quad++ = '.';
            i = quad - (char *) dest;
            StrUtil::intToString( ((Rai_u8 *) src)[ 1 ], quad, dsize - i,
                                  U_DECIMAL, false, &quad );
            *quad++ = '.';
            i = quad - (char *) dest;
            StrUtil::intToString( ((Rai_u8 *) src)[ 2 ], quad, dsize - i,
                                  U_DECIMAL, false, &quad );
            *quad++ = '.';
            i = quad - (char *) dest;
            StrUtil::intToString( ((Rai_u8 *) src)[ 3 ], quad, dsize - i );
          }
          else if ( ssize == 2 ) {
            StrUtil::intToString( *(Rai_u16 *) src, (char *) dest, dsize );
          }
          else {
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_IPDATA );
          }
          break;
        case RAIMSG_INT:
        case RAIMSG_UINT:
          if ( dsize == 4 ) {
            if ( ssize == 4 ) {
              ::memcpy( dest, src, 4 );
              break;
            }
          }
          if ( dsize == 2 ) {
            if ( ssize == 2 ) {
              ::memcpy( dest, src, 2 );
              break;
            }
          }
        /* FALLTHRU */
        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_IPDATA );
      }
      break;
    case RAIMSG_BOOLEAN:
      if ( ssize != 1 )
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_BOOL );
      switch ( dtype ) {
        case RAIMSG_STRING:
          if ( dsize > 6 )
            dsize = 6;
          if ( ( *(Rai_u8 *) src ) == 0 )
            ::memcpy( (char *) dest, "false", dsize < 6 ? dsize : 6 );
          else
            ::memcpy( (char *) dest, "true", dsize < 5 ? dsize : 5 );
          break;
        case RAIMSG_BOOLEAN:
          *(Rai_u8 *) dest = ( *(Rai_u8 *) src ) ? 1 : 0;
          break;
        case RAIMSG_INT:
        case RAIMSG_UINT:
          if ( dsize == 4 )
            *(Rai_u32 *) dest = ( *(Rai_u8 *) src ) ? 1 : 0;
          else if ( dsize == 2 )
            *(Rai_u16 *) dest = ( *(Rai_u8 *) src ) ? 1 : 0;
          else if ( dsize == 1 )
            *(Rai_u8 *) dest = ( *(Rai_u8 *) src ) ? 1 : 0;
          else if ( dsize == 8 )
            *(Rai_u64 *) dest = ( *(Rai_u8 *) src ) ? 1 : 0;
          else
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
          break;
        case RAIMSG_REAL:
          if ( dsize == 8 )
            *(Rai_f64 *) dest = ( *(Rai_u8 *) src ) ? 1.0 : 0.0;
          else if ( dsize == 4 )
            *(Rai_f32 *) dest = (Rai_f32) ( ( *(Rai_u8 *) src ) ? 1.0 : 0.0 );
          else
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
          break;
        default:
          throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_BOOL );
      }
      break;
    case RAIMSG_INT:
      if ( ssize == 4 ) {
        i32 = *(Rai_i32 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( i32, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( i32 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) i32;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) i32;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) i32;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) i32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) i32;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) i32;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) i32;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) i32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) i32;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) i32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 2 ) {
        i16 = *(Rai_i16 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( i16, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( i16 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) i16;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) i16;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) i16;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) i16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) i16;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) i16;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) i16;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) i16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) i16;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) i16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 1 ) {
        i8 = *(Rai_i8 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( i8, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( i8 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) i8;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) i8;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) i8;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) i8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) i8;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) i8;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) i8;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) i8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) i8;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) i8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 8 ) {
        i64 = *(Rai_i64 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( i64, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( i64 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) i64;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) i64;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) i64;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) i64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) i64;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) i64;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) i64;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) i64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) i64;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) i64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
      break;
    case RAIMSG_UINT:
      if ( ssize == 4 ) {
        u32 = *(Rai_u32 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( u32, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( u32 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) u32;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) u32;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) u32;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) u32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) u32;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) u32;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) u32;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) u32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) u32;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) u32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 2 ) {
        u16 = *(Rai_u16 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( u16, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( u16 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) u16;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) u16;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) u16;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) u16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) u16;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) u16;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) u16;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) u16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) u16;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) u16;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 1 ) {
        u8 = *(Rai_u8 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( u8, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( u8 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) u8;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) u8;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) u8;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) u8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) u8;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) u8;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) u8;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) u8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) u8;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) u8;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else if ( ssize == 8 ) {
        u64 = *(Rai_u64 *) src;
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::intToString( u64, (char *) dest, dsize );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( u64 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) u64;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) u64;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) u64;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) u64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) u64;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) u64;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) u64;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) u64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) u64;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) u64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
        }
      }
      else
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
      break;
    case RAIMSG_REAL:
      if ( ssize == 8 ) {
        f64 = *(Rai_f64 *) src;
        if ( isnan( f64 ) || isinf( f64 ) ) {
          switch ( dtype ) {
            case RAIMSG_BOOLEAN:
            case RAIMSG_INT:
            case RAIMSG_UINT: f64 = 0; break;
            default: break;
          }
        }
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::floatToString( f64, (char *) dest, dsize,
                                    realToStringPrecision( shint ) );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( f64 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) f64;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) f64;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) f64;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) f64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) f64;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) f64;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) f64;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) f64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) f64;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) f64;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
        }
      }
      else if ( ssize == 4 ) {
        f32 = *(Rai_f32 *) src;
        if ( isnan( f32 ) || isinf( f32 ) ) {
          switch ( dtype ) {
            case RAIMSG_BOOLEAN:
            case RAIMSG_INT:
            case RAIMSG_UINT: f32 = 0; break;
            default: break;
          }
        }
        switch ( dtype ) {
          case RAIMSG_STRING:
            StrUtil::floatToString( f32, (char *) dest, dsize,
                                    realToStringPrecision( shint ) );
            break;
          case RAIMSG_BOOLEAN:
            *(Rai_u8 *) dest = ( f32 ? 1 : 0 );
            break;
          case RAIMSG_INT:
            if ( dsize == 4 )
              *(Rai_i32 *) dest = (Rai_i32) f32;
            else if ( dsize == 2 )
              *(Rai_i16 *) dest = (Rai_i16) f32;
            else if ( dsize == 1 )
              *(Rai_i8 *) dest = (Rai_i8) f32;
            else if ( dsize == 8 )
              *(Rai_i64 *) dest = (Rai_i64) f32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_UINT:
            if ( dsize == 4 )
              *(Rai_u32 *) dest = (Rai_u32) f32;
            else if ( dsize == 2 )
              *(Rai_u16 *) dest = (Rai_u16) f32;
            else if ( dsize == 1 )
              *(Rai_u8 *) dest = (Rai_u8) f32;
            else if ( dsize == 8 )
              *(Rai_u64 *) dest = (Rai_u64) f32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_INT );
            break;
          case RAIMSG_REAL:
            if ( dsize == 8 )
              *(Rai_f64 *) dest = (Rai_f64) f32;
            else if ( dsize == 4 )
              *(Rai_f32 *) dest = (Rai_f32) f32;
            else
              throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
            break;
          default:
            throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
        }
      }
      else
        throw RaiMsgErr::getErr( RaiMsgErr::BAD_CVT_REAL );
      break;
    default:
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  }
}


RaiMsg_data
RaiField::AlignData( RaiField_data &val )
{
  if ( this->data == NULL )
    return NULL;
  switch ( this->type ) {
    case RAIMSG_BOOLEAN:
      val.boolean = *(Rai_u8 *) this->data;
      break;
    case RAIMSG_INT:
    case RAIMSG_UINT:
      if ( this->size == 4 ) {
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.u32 );
      }
      else if ( this->size == 2 ) {
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.u16 );
      }
      else if ( this->size == 8 ) {
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.u64 );
      }
      else {
        val.u8 = *(Rai_u8 *) this->data;
      }
      break;
    case RAIMSG_REAL:
      if ( this->size == 8 )
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.u64 );
      else
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.u32 );
      break;
    case RAIMSG_IPDATA:
      if ( this->size == 4 )
        ::memcpy( &val.ipaddr, this->data, sizeof( val.ipaddr ) );
      else
        Unaligned::endianGetInt( (Rai_u8 *) this->data, val.ipport );
      break;
    default:
      return this->data;
  }

  return (RaiMsg_data) &val;
}


RaiMsg_data
RaiField::AlignHintData( RaiField_data &val )
{
  switch ( this->hintType ) {
    case RAIMSG_BOOLEAN:
      val.boolean = *(Rai_u8 *) this->hintData;
      break;
    case RAIMSG_INT:
    case RAIMSG_UINT:
      if ( this->hintSize == 4 )
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.u32 );
      else if ( this->hintSize == 2 )
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.u16 );
      else if ( this->hintSize == 8 )
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.u64 );
      else
        val.u8 = *(Rai_u8 *) this->hintData;
      break;
    case RAIMSG_REAL:
      if ( this->hintSize == 8 )
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.u64 );
      else
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.u32 );
      break;
    case RAIMSG_IPDATA:
      if ( this->hintSize == 4 )
        ::memcpy( &val.ipaddr, this->hintData, sizeof( val.ipaddr ) );
      else
        Unaligned::endianGetInt( (Rai_u8 *) this->hintData, val.ipport );
      break;
    default:
      return this->hintData;
  }

  return (RaiMsg_data) &val;
}


void
RaiField::Convert( bool &b )
{
  RaiField_data data;
  Rai_u8        u8;
  RaiField::Convert( RAIMSG_BOOLEAN, 1, (RaiMsg_data) &u8, this->type,
                     this->size, this->AlignData( data ) );
  b = ( u8 == 0 ? false : true );
}


void
RaiField::Convert( Rai_i8 &i8 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_INT, 1, (RaiMsg_data) &i8, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_u8 &u8 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_UINT, 1, (RaiMsg_data) &u8, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_i16 &i16 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_INT, 2, (RaiMsg_data) &i16, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_u16 &u16 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_UINT, 2, (RaiMsg_data) &u16, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_i32 &i32 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_INT, 4, (RaiMsg_data) &i32, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_u32 &u32 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_UINT, 4, (RaiMsg_data) &u32, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_i64 &i64 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_INT, 8, (RaiMsg_data) &i64, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_u64 &u64 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_UINT, 8, (RaiMsg_data) &u64, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_f32 &f32 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_REAL, 4, (RaiMsg_data) &f32, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_f64 &f64 )
{
  RaiField_data data;
  RaiField::Convert( RAIMSG_REAL, 8, (RaiMsg_data) &f64, this->type, this->size,
                     this->AlignData( data ) );
}


void
RaiField::Convert( Rai_f32 &f32,  ConvertCtx &ctx )
{
  RaiField_data data;

  ctx.init();
  this->SrcConvertCtx( ctx, data );
  ctx.destType = RAIMSG_REAL;
  ctx.destSize = sizeof( Rai_f32 );
  ctx.destData = (RaiMsg_data) &f32;

  RaiField::Convert( ctx );
}


void
RaiField::Convert( Rai_f64 &f64,  ConvertCtx &ctx )
{
  RaiField_data data;

  ctx.init();
  this->SrcConvertCtx( ctx, data );
  ctx.destType = RAIMSG_REAL;
  ctx.destSize = sizeof( Rai_f64 );
  ctx.destData = (RaiMsg_data) &f64;

  RaiField::Convert( ctx );
}


void
RaiField::Convert( char *str,  RaiMsg_size limit )
{
  ConvertCtx    ctx;
  RaiField_data data;

  ctx.init();
  this->SrcConvertCtx( ctx, data );
  ctx.destType = RAIMSG_STRING;
  ctx.destSize = limit;
  ctx.destData = (RaiMsg_data) str;
  if ( this->type == RAIMSG_REAL &&
       ( this->hintType == RAIMSG_UINT ||
         this->hintType == RAIMSG_INT ) && this->hintSize == 1 )
    ctx.srcHint = ((const byte *) this->hintData)[ 0 ];

  RaiField::Convert( ctx );
/*  RaiField::Convert( RAIMSG_STRING, limit, (RaiMsg_data) str, this->type,
                     this->size, this->AlignData( data ) );*/
}


void
RaiField::HintConvert( bool &b )
{
  RaiField_data data;
  Rai_u8        u8;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_BOOLEAN, 1, (RaiMsg_data) &u8, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
  b = ( u8 == 0 ? false : true );
}


void
RaiField::HintConvert( Rai_i8 &i8 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_INT, 1, (RaiMsg_data) &i8, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_u8 &u8 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_UINT, 1, (RaiMsg_data) &u8, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_i16 &i16 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_INT, 2, (RaiMsg_data) &i16, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_u16 &u16 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_UINT, 2, (RaiMsg_data) &u16, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_i32 &i32 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_INT, 4, (RaiMsg_data) &i32, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_u32 &u32 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_UINT, 4, (RaiMsg_data) &u32, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_i64 &i64 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_INT, 8, (RaiMsg_data) &i64, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_u64 &u64 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_UINT, 8, (RaiMsg_data) &u64, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_f32 &f32 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_REAL, 4, (RaiMsg_data) &f32, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( Rai_f64 &f64 )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_REAL, 8, (RaiMsg_data) &f64, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( char *str,  RaiMsg_size limit )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( RAIMSG_STRING, limit, (RaiMsg_data) str, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


void
RaiField::HintConvert( RaiMsg_type dest_type,  RaiMsg_size dest_size,
                       RaiMsg_data dest_data )
{
  RaiField_data data;
  if ( this->type == RAIMSG_ARRAY || this->type == RAIMSG_PARTIAL )
    throw RaiMsgErr::getErr( RaiMsgErr::BAD_ARG );
  RaiField::Convert( dest_type, dest_size, dest_data, this->hintType,
                     this->hintSize, this->AlignHintData( data ) );
}


bool
RaiField::Overlaps( const RaiMsg_data p2,  RaiMsg_size s2 ) const
{
  const RaiMsg * msg;

  if ( RaiMsg::Overlaps( (RaiMsg_data) this->name, this->nameLen, p2, s2 ) )
    return true;

  switch ( this->type ) {
    case RAIMSG_MESSAGE:
      if ( (msg = (const RaiMsg *) this->data) != NULL )
        if ( RaiMsg::Overlaps( msg->msgBuf, msg->msgSize, p2, s2 ) )
          return true;
      /* fall through */
    case RAIMSG_NODATA:
      goto test_hint_data;

    case RAIMSG_ARRAY:
    case RAIMSG_PARTIAL:
      return RaiMsg::Overlaps( this->data, this->size, p2, s2 );

    default:
      if ( RaiMsg::Overlaps( this->data, this->size, p2, s2 ) )
        return true;
    test_hint_data:;
      if ( this->hintType != RAIMSG_NODATA )
        return RaiMsg::Overlaps( this->hintData, this->hintSize, p2, s2 );
      break;
  }
  return false;
}


RaiMsg_size
RaiField::CopySize( const RaiMsg_dict *entry )
{
  RaiMsg     * msg;
  unsigned int off = 0;

  if ( entry == NULL && this->nameLen != 0 )
    off += this->nameLen;

  switch ( this->type ) {
    case RAIMSG_MESSAGE:
      if ( (msg = (RaiMsg *) this->data) != NULL )
        off += msg->msgSize;
      /* fall through */
    case RAIMSG_NODATA:
      goto add_hint_data;

    case RAIMSG_ARRAY:
    case RAIMSG_PARTIAL:
      off += this->size;
      break;

    default:
      off += this->size;
    add_hint_data:;
      if ( this->hintType != RAIMSG_NODATA )
        off += this->hintSize;
      break;
  }
  return off;
}


RaiMsg_data
RaiField::CopyTo( RaiField &toFld,  Rai_u8 *buf,  RaiMsg_size bufSize,
                  const RaiMsg_dict *entry )
{
  RaiMsg_data ptr = NULL;
  RaiMsg_size off = this->CopySize( entry );

  if ( off > bufSize ) {
    if ( bufSize != 0 )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_BUFFER );
    MALLOC( off, &ptr );
    buf = (Rai_u8 *) ptr;
  }

  this->CopyTo( toFld, buf, entry );
  return ptr;
}


RaiField *
RaiField::Copy( Rai_u8 *buf,  RaiMsg_size bufSize,
                const RaiMsg_dict *entry )
{
  RaiField  * toFld;
  void      * ptr = (void *) buf;

  try {
    RaiMsg_size sz = sizeof( RaiField ) + this->CopySize();
    if ( ptr == NULL )
      MALLOC( sz, &ptr );
    else if ( bufSize < sz )
      throw RaiMsgErr::getErr( RaiMsgErr::BAD_BUFFER );
    toFld = new ( ptr ) RaiField();
    return this->CopyTo( *toFld, (Rai_u8 *) &toFld[ 1 ], entry );
  } catch ( ... ) {
    if ( ptr != buf )
      FREE( ptr );
    throw;
  }
}


RaiField *
RaiField::CopyTo( RaiField &toFld,  Rai_u8 *buf,
                  const RaiMsg_dict *entry )
{
  RaiMsg    * msg;
  RaiMsg_size off;

  off = 0;
  if ( entry != NULL ) {
    toFld.name    = entry->fname;
    toFld.nameLen = entry->fname_size;
  }
  else if ( this->nameLen != 0 ) {
    ::memcpy( buf, this->name, this->nameLen );
    off          += this->nameLen;
    toFld.name    = (RaiMsg_name) buf;
    toFld.nameLen = this->nameLen;
  }
  else {
    toFld.name    = NULL;
    toFld.nameLen = 0;
  }

  toFld.size = this->size;
  toFld.type = this->type;
  switch ( this->type ) {
    case RAIMSG_MESSAGE:
      if ( (msg = (RaiMsg *) this->data) != NULL ) {
        ::memcpy( &buf[ off ], msg->msgBuf, msg->msgSize );
        toFld.tempMsg.InitSubMessage( &buf[ off ], msg->msgSize, NULL,
                                      msg->proto, msg->isDynamic );
        toFld.data = &toFld.tempMsg;
        off += msg->msgSize;
      }
      else {
        toFld.data = NULL;
      }
      /* fall through */
    case RAIMSG_NODATA:
      goto copy_hint_data;

    case RAIMSG_ARRAY:
    case RAIMSG_PARTIAL:
      ::memcpy( &buf[ off ], this->data, this->size );
      toFld.data = (RaiMsg_data) &buf[ off ];
      off += this->size;
      toFld.hintType = this->hintType; /* element type */
      toFld.hintSize = this->hintSize; /* offset or element size */
      break;

    default:
      ::memcpy( &buf[ off ], this->data, this->size );
      toFld.data = (RaiMsg_data) &buf[ off ];
      off += this->size;
    copy_hint_data:;
      if ( (toFld.hintType = this->hintType) != RAIMSG_NODATA ) {
        ::memcpy( &buf[ off ], this->hintData, this->hintSize );
        toFld.hintData = (RaiMsg_data) &buf[ off ];
        off += this->hintSize;
      }
      break;
  }

  return &toFld;
}

