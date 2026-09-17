/* Copyright (c) 2003 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com */
#ifndef __rai_msg__field_h__
#define __rai_msg__field_h__

#if ! defined( RAIMSG_DLL_EXP )
#if defined( RAI_DLL )
#define RAIMSG_DLL_EXP __declspec(dllimport)
#else
#define RAIMSG_DLL_EXP
#endif
#endif

#ifndef __rai_msg__msg_h__
#include "msg/msg.h"
#endif

#ifndef __rai_util__int_bits_h__
#include "util/int_bits.h"
#endif

#ifndef __rai_msg__sass_const_h__
#include "msg/sass_const.h"
#endif

union RaiField_data {
  Rai_u8     boolean;
  Rai_i8     i8;
  Rai_u8     u8;
  Rai_i16    i16;
  Rai_u16    u16;
  Rai_i32    i32;
  Rai_u32    u32;
  Rai_i64    i64;
  Rai_u64    u64;
  Rai_f32    f32;
  Rai_f64    f64;
  Rai_ipport ipport;
  Rai_ipaddr ipaddr;
  Rai_u16    td[ 3 ];
  char       str[ 24 ];
};


struct RAIMSG_DLL_EXP RaiField {
  RaiMsg_name   name;
  RaiMsg_type   type;
  RaiMsg_size   size;
  RaiMsg_data   data;
  RaiMsg_type   hintType;
  RaiMsg_size   hintSize;
  RaiMsg_data   hintData;
  RaiField_data updateData;
  RaiField_data updateHintData;
  RaiMsg_size   fieldStart,
                fieldEnd,
                nameLen,
                iterMsgSize;
  RaiMsg      * iterMsg,
                tempMsg;
  static unsigned int cvtFloatToStringPrecision; /* default = 255 or environ var
                   RAIMSG_CVT_PRECISION;  number of decimal places to print
                   when converting floats to string */
  static unsigned int overrideFloatToStringPrecision; /* default = 0 */
  void ReUse( void );

  void * operator new( size_t /* sz */, void *ptr ) { return ptr; }

  SYS_OPS( RaiField );
  RaiField() {
    this->iterMsg  = NULL;
    this->name     = NULL;
    this->nameLen  = 0;
    this->type     = RAIMSG_NODATA;
    this->hintType = RAIMSG_NODATA;
  }
  /* Field data should already be packed in network order */
  void InitRaw( RaiMsg_name name,  RaiMsg_size nameLen,  RaiMsg_type type,
                RaiMsg_size size,  RaiMsg_data data ) {
    this->name    = name;
    this->nameLen = nameLen;
    this->type    = type;
    this->size    = size;
    this->data    = data;
  }
  RaiField( RaiMsg_name name,  RaiMsg_type type,  RaiMsg_size size,
            RaiMsg_data data );
  RaiField( RaiMsg_name name,  RaiMsg_type type,  RaiMsg_size size,
            RaiMsg_data data,  RaiMsg_type hint_type,  RaiMsg_size hint_size,
            RaiMsg_data hint_data );

  RaiMsg_name Name( void ) const {
    return this->name;
  }

  bool isNamed( const char *name ) {
    if ( this->name != NULL && name != NULL )
      return ::strcmp( this->name, name ) == 0;
    return this->name == name;
  }

  RaiMsg_size NameSize( void ) const {
    return this->nameLen;
  }

  bool Fid( Rai_u16 &fid ) const {
    if ( this->nameLen >= 3 && this->name[ this->nameLen - 3 ] == '\0' ) {
      rai::Unaligned::endianGetInt( (Rai_u8 *) &this->name[ this->nameLen - 2 ],
                                    fid );
      return true;
    }
    return false;
  }

  RaiMsg_type Type( void ) const {
    return this->type;
  }

  RaiMsg_size Size( void ) const {
    return this->size;
  }

  static bool isValidMachineType( RaiMsg_type type,  RaiMsg_size size ) {
    static const unsigned int valid[ 9 ] = {
      /* 0 */ ( 1 << RAIMSG_BOOLEAN ) | ( 1 << RAIMSG_INT ) |
              ( 1 << RAIMSG_UINT ) | ( 1 << RAIMSG_REAL ) |
              ( 1 << RAIMSG_IPDATA ),
      /* 1 */ ( 1 << RAIMSG_BOOLEAN ) | ( 1 << RAIMSG_INT ) |
              ( 1 << RAIMSG_UINT ),
      /* 2 */ ( 1 << RAIMSG_INT ) | ( 1 << RAIMSG_UINT ) |
              ( 1 << RAIMSG_IPDATA ),
      /* 3 */ 0,
      /* 4 */ ( 1 << RAIMSG_INT ) | ( 1 << RAIMSG_UINT ) |
              ( 1 << RAIMSG_REAL ) | ( 1 << RAIMSG_IPDATA ),
      /* 5 */ 0, /* 6 */ 0, /* 7 */ 0,
      /* 8 */ ( 1 << RAIMSG_INT ) | ( 1 << RAIMSG_UINT ) |
              ( 1 << RAIMSG_REAL )
    };
    unsigned int t = (unsigned int) type;
    if ( size > 8U || t > 31U )
      return false;
    if ( ( valid[ size ] & ( 1 << t ) ) == 0 )
      return false;
    return true;
  }

  static bool isValidArrayType( RaiMsg_type type,  RaiMsg_size size ) {
    if ( ( type == RAIMSG_STRING || type == RAIMSG_OPAQUE ) && size > 0 )
      return true;
    return isValidMachineType( type, size );
  }
  static RaiMsg_size MakeFidName( RaiMsg_name fname,  Rai_u16 fid,
                                  char buf[ 256 ] );
  RaiMsg_data Data( void );

  RaiMsg_type HintType( void ) const {
    return this->hintType;
  }

  RaiMsg_size HintSize( void ) const {
    if ( this->hintType == RAIMSG_NODATA )
      return 0;
    return this->hintSize;
  }

  RaiMsg_data HintData( void ) const {
    if ( this->hintType == RAIMSG_NODATA )
      return NULL;
    return this->hintData;
  }

  RaiMsg_size Offset( void ) const {
    if ( this->type == RAIMSG_PARTIAL )
      return this->hintSize;
    return 0;
  }

  RaiMsg_type EntryType( void ) const {
    return this->hintType;
  }

  RaiMsg_size EntrySize( void ) const {
    return this->hintSize;
  }

  RaiMsg_size NumEntries( void ) const {
    if ( this->hintSize == 0 )
      return 0;
    return this->size / this->hintSize;
  }

  struct ConvertCtx {
    RaiMsg_type destType;
    RaiMsg_size destSize;
    RaiMsg_data destData;
    Rai_u8      destHint;
    RaiMsg_type srcType;
    RaiMsg_size srcSize;
    RaiMsg_data srcData;
    Rai_u8      srcHint;
    void init( void ) { ::memset( this, 0, sizeof( *this ) ); }
    void init( RaiMsg_type dtype,  RaiMsg_size dsize,
               RaiMsg_data dest,  RaiMsg_type stype,
               RaiMsg_size ssize,  RaiMsg_data src ) {
      this->destType = dtype; this->destSize = dsize; this->destData = dest;
      this->destHint = 0;
      this->srcType  = stype; this->srcSize  = ssize; this->srcData  = src;
      this->srcHint  = 0;
    }
  };
  static void Convert( RaiMsg_type dtype,  RaiMsg_size dsize,
                       RaiMsg_data dest,  RaiMsg_type stype,
                       RaiMsg_size ssize,  RaiMsg_data src )
 {
    ConvertCtx ctx;
    ctx.init( dtype, dsize, dest, stype, ssize, src );
    RaiField::Convert( ctx );
  }
  static void Convert( ConvertCtx &ctx );

  RaiMsg_data AlignData( RaiField_data &val );

  RaiMsg_data AlignHintData( RaiField_data &val );

  void SrcConvertCtx( ConvertCtx &srcCtx, RaiField_data &srcData ) {
    srcCtx.srcType = this->type;
    srcCtx.srcSize = this->size;
    srcCtx.srcData = this->AlignData( srcData );
    srcCtx.srcHint = 0;
  }
  void Convert( bool &b );

  void Convert( Rai_i8 &i8 );

  void Convert( Rai_u8 &u8 );

  void Convert( Rai_i16 &i16 );

  void Convert( Rai_u16 &u16 );

  void Convert( Rai_i32 &i32 );

  void Convert( Rai_u32 &u32 );

  void Convert( Rai_i64 &i64 );

  void Convert( Rai_u64 &u64 );

  void Convert( Rai_f32 &f32 );

  void Convert( Rai_f64 &f64 );

  void Convert( Rai_f32 &f32,  ConvertCtx &ctx );

  void Convert( Rai_f64 &f64,  ConvertCtx &ctx );

  void Convert( char *str,  RaiMsg_size limit );

  void Get( bool &b ) {
    if ( this->type == RAIMSG_BOOLEAN && this->size == 1 )
      b = ( *(Rai_u8 *) this->data ? true : false );
    else
      this->Convert( b );
  }
  void Get( Rai_i8 &i8 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 1 )
      i8 = *(Rai_i8 *) this->data;
    else
      this->Convert( i8 );
  }
  void Get( Rai_u8 &u8 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 1 )
      u8 = *(Rai_u8 *) this->data;
    else
      this->Convert( u8 );
  }
  void Get( Rai_i16 &i16 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 2 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, i16 );
    else
      this->Convert( i16 );
  }
  void Get( Rai_u16 &u16 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 2 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, u16 );
    else
      this->Convert( u16 );
  }
  void Get( Rai_i32 &i32 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 4 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, i32 );
    else
      this->Convert( i32 );
  }
  void Get( Rai_u32 &u32 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
        this->size == 4 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, u32 );
    else
      this->Convert( u32 );
  }
  void Get( Rai_i64 &i64 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 8 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, i64 );
    else
      this->Convert( i64 );
  }
  void Get( Rai_u64 &u64 ) {
    if ( ( this->type == RAIMSG_INT || this->type == RAIMSG_UINT ) &&
         this->size == 8 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->data, u64 );
    else
      this->Convert( u64 );
  }
  void Get( Rai_f32 &f32 ) {
    if ( this->type == RAIMSG_REAL && this->size == 4 )
      rai::Unaligned::endianGetFloat( (Rai_u8 *) this->data, f32 );
    else
      this->Convert( f32 );
  }
  void Get( Rai_f64 &f64 ) {
    if ( this->type == RAIMSG_REAL && this->size == 8 )
      rai::Unaligned::endianGetFloat( (Rai_u8 *) this->data, f64 );
    else
      this->Convert( f64 );
  }
  void Get( rai::SassConst::MsgType &m ) {
    if ( this->type != RAIMSG_STRING ) {
      Rai_u16 tmp;
      this->Get( tmp );
      m = (rai::SassConst::MsgType) tmp;
    }
    else {
      const char *s;
      this->Get( s );
      m = (rai::SassConst::MsgType) rai::SassConst::stringToMsgType( s );
    }
  }
  void Get( rai::SassConst::RecStatus &r ) {
    if ( this->type != RAIMSG_STRING ) {
      Rai_u16 tmp;
      this->Get( tmp );
      r = (rai::SassConst::RecStatus) tmp;
    }
    else {
      const char *s;
      this->Get( s );
      r = (rai::SassConst::RecStatus) rai::SassConst::stringToRecStatus( s );
    }
  }
  void Get( const char *&str ) {
    if ( this->type == RAIMSG_STRING || this->type == RAIMSG_OPAQUE )
      str = (char *) this->data;
    else
      throw RaiMsgErr::getErr( RaiMsgErr::NOT_STRING_FIELD );
  }
  void Get( const byte *&str ) {
    if ( this->type == RAIMSG_STRING || this->type == RAIMSG_OPAQUE )
      str = (byte *) this->data;
    else
      throw RaiMsgErr::getErr( RaiMsgErr::NOT_STRING_FIELD );
  }
  void Get( char *str,  unsigned int limit ) {
    if ( this->type == RAIMSG_STRING || this->type == RAIMSG_OPAQUE ) {
      if ( limit > this->size ) {
        ::memcpy( str, this->data, this->size );
        str[ this->size ] = '\0';
      }
      else {
        ::memcpy( str, (const char *) this->data, limit );
      }
    }
    else {
      this->Convert( str, limit );
    }
  }
  void Get( RaiMsg_type ftype,  RaiMsg_size fsize,
            RaiMsg_data fdata ) {
    RaiField_data tmp;
    RaiField::Convert( ftype, fsize, fdata, this->type, this->size,
                       this->AlignData( tmp ) );
  }
  void Get( RaiMsg &msg );

  void GetHint( bool &b ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         this->hintType == RAIMSG_BOOLEAN && this->hintSize == 1 )
      b = ( *(Rai_u8 *) this->hintData ? true : false );
    else
      this->HintConvert( b );
  }
  void GetHint( Rai_i8 &i8 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 1 )
      i8 = *(Rai_i8 *) this->hintData;
    else
      this->HintConvert( i8 );
  }
  void GetHint( Rai_u8 &u8 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 1 )
      u8 = *(Rai_u8 *) this->hintData;
    else
      this->HintConvert( u8 );
  }
  void GetHint( Rai_i16 &i16 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 2 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, i16 );
    else
      this->HintConvert( i16 );
  }
  void GetHint( Rai_u16 &u16 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 2 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, u16 );
    else
      this->HintConvert( u16 );
  }
  void GetHint( Rai_i32 &i32 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 4 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, i32 );
    else
      this->HintConvert( i32 );
  }
  void GetHint( Rai_u32 &u32 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 4 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, u32 );
    else
      this->HintConvert( u32 );
  }
  void GetHint( Rai_i64 &i64 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 8 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, i64 );
    else
      this->HintConvert( i64 );
  }
  void GetHint( Rai_u64 &u64 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         ( this->hintType == RAIMSG_INT || this->hintType == RAIMSG_UINT ) &&
         this->hintSize == 8 )
      rai::Unaligned::endianGetInt( (Rai_u8 *) this->hintData, u64 );
    else
      this->HintConvert( u64 );
  }
  void GetHint( Rai_f32 &f32 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         this->hintType == RAIMSG_REAL && this->hintSize == 4 )
      rai::Unaligned::endianGetFloat( (Rai_u8 *) this->hintData, f32 );
    else
      this->HintConvert( f32 );
  }
  void GetHint( Rai_f64 &f64 ) {
    if ( this->type != RAIMSG_ARRAY && this->type != RAIMSG_PARTIAL &&
         this->hintType == RAIMSG_REAL && this->hintSize == 8 )
      rai::Unaligned::endianGetFloat( (Rai_u8 *) this->hintData, f64 );
    else
      this->HintConvert( f64 );
  }
  void HintConvert( bool &b );

  void HintConvert( Rai_i8 &i8 );

  void HintConvert( Rai_u8 &u8 );

  void HintConvert( Rai_i16 &i16 );

  void HintConvert( Rai_u16 &u16 );

  void HintConvert( Rai_i32 &i32 );

  void HintConvert( Rai_u32 &u32 );

  void HintConvert( Rai_i64 &i64 );

  void HintConvert( Rai_u64 &u64 );

  void HintConvert( Rai_f32 &f32 );

  void HintConvert( Rai_f64 &f64 );

  void HintConvert( char *str,  RaiMsg_size limit );

  void HintConvert( const char * ) {}

  void HintConvert( RaiMsg_type dest_type,  RaiMsg_size dest_size,
                    RaiMsg_data dest_data );
  RaiMsg_size PackSize( RaiMsg_protocol proto );

  RaiMsg_size PackSize( void ) {
    return this->PackSize( RAIMSG_PROTO );
  }
  void Pack( RaiMsg_protocol proto,  Rai_u8 *to_ptr );

  void Pack( Rai_u8 *to_ptr ) {
    return this->Pack( RAIMSG_PROTO, to_ptr );
  }
  void UnPack( Rai_u8 *from_ptr );

  Rai_u8 *UnPack( RaiMsg_protocol proto,  Rai_u8 *from_ptr,
                  unsigned int length );
  void Update( RaiField *field_ptr );

  static inline unsigned int FNAME_LEN( RaiMsg_name fname ) {
    return ( fname == NULL ) ? 0 : (unsigned int) ::strlen( fname ) + 1;
  }
  void Update( RaiMsg_name fname,  Rai_i8 i8 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), i8 );
  }
  void Update( RaiMsg_name fname,  Rai_u8 u8 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), u8 );
  }
  void Update( RaiMsg_name fname,  Rai_i16 i16 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), i16 );
  }
  void Update( RaiMsg_name fname,  Rai_u16 u16 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), u16 );
  }
  void Update( RaiMsg_name fname,  Rai_i32 i32 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), i32 );
  }
  void Update( RaiMsg_name fname,  Rai_u32 u32 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), u32 );
  }
  void Update( RaiMsg_name fname,  Rai_i64 i64 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), i64 );
  }
  void Update( RaiMsg_name fname,  Rai_u64 u64 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), u64 );
  }
  void Update( RaiMsg_name fname,  Rai_f32 f32 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), f32 );
  }
  void Update( RaiMsg_name fname,  Rai_f64 f64 ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), f64 );
  }
  void Update( RaiMsg_name fname,  const char *str ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), str );
  }
  void Update( RaiMsg_name fname,  RaiMsg *msg_ptr ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), msg_ptr );
  }
  void Update( RaiMsg_name fname,  bool b ) {
    this->UpdateEx( fname, FNAME_LEN( fname ), b );
  }
  /* update partial data, which is opaque with an offset in the hint */
  void Update( RaiMsg_name fname,  RaiMsg_data partial_data,
               RaiMsg_size partial_size,  RaiMsg_size offset );
  /* same as above except with field name length */
  void UpdateEx( RaiMsg_name fname, RaiMsg_size fnameLen,
                 RaiMsg_data partial_data,  RaiMsg_size partial_size,
                 RaiMsg_size offset );
  /* update array data, blob with element size and type */
  void Update( RaiMsg_name fname,  RaiMsg_data array_data,
               RaiMsg_size num_entries,  RaiMsg_type entry_type,
               RaiMsg_size entry_size );
  /* same as above except with field name length */
  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,
                 RaiMsg_data array_data,  RaiMsg_size num_entries,
                 RaiMsg_type entry_type,  RaiMsg_size entry_size )
;
  /* update anthing except array and partial, fdata is a pointer, but not a
   * pointer to a pointer in the case of a string and opaque data */
  void Update( RaiMsg_name fname,  RaiMsg_type ftype,
               RaiMsg_size fsize,  RaiMsg_data fdata );
  /* same as above except with field name length */
  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  RaiMsg_type ftype,
                 RaiMsg_size fsize,  RaiMsg_data fdata )
;
  /* same as above except data is already in big endian wire format */
  void UpdateRaw( RaiMsg_name fname,  RaiMsg_size fnameLen,  RaiMsg_type ftype,
                  RaiMsg_size fsize,  RaiMsg_data fdata )
;
  /* update any data except array and partion */
  void Update( RaiMsg_name fname,  RaiMsg_type ftype,
               RaiMsg_size fsize,  RaiMsg_data fdata,
               RaiMsg_type hint_type,  RaiMsg_size hint_size,
               RaiMsg_data hint_data );
  /* with name len */
  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,
                 RaiMsg_type ftype,  RaiMsg_size fsize,
                 RaiMsg_data fdata,  RaiMsg_type hint_type,
                 RaiMsg_size hint_size,  RaiMsg_data hint_data )
;
  /* same as above except data is already big endian */
  void UpdateRaw( RaiMsg_name fname,  RaiMsg_size fnameLen,  RaiMsg_type ftype,
                  RaiMsg_size fsize,  RaiMsg_data fdata,
                  RaiMsg_type hint_type,  RaiMsg_size hint_size,
                  RaiMsg_data hint_data );
  void Rename( RaiMsg_name fname ) {
    this->RenameEx( fname, FNAME_LEN( fname ) );
  }
  void RenameEx( RaiMsg_name new_fname,  RaiMsg_size fnameLen ) {
    this->name    = new_fname;
    this->nameLen = fnameLen;
  }
  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i8 i8 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u8 u8 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i16 i16 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u16 u16 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i32 i32 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u32 u32 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_i64 i64 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_u64 u64 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_f32 f32 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  Rai_f64 f64 );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  const char *str );

  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  RaiMsg *msg_ptr )
;
  void UpdateEx( RaiMsg_name fname,  RaiMsg_size fnameLen,  bool b );

  bool Find( RaiMsg *msg,  RaiMsg_name fname ) {
    return this->FindEx( msg, fname, FNAME_LEN( fname ) );
  }
  bool FindEx( RaiMsg *msg,  RaiMsg_name fname,  RaiMsg_size fnameLen )
;
  bool FindFid( RaiMsg *msg,  Rai_u16 fid );

  bool Find( RaiMsg *msg,  const RaiMsg_dict *entry );

  bool First( RaiMsg *msg );

  bool Next( void );

  bool First( RaiMsg *msg,  const RaiMsg_dict *&entry );

  bool Next( const RaiMsg_dict *&entry );

  bool First( RaiMsg *msg,  const RaiMsg_form *form,
              const RaiMsg_dict *&entry );
  bool Next( const RaiMsg_form *form,  const RaiMsg_dict *&entry )
;
  /* return true and get value if has first field and it's named fname */
  template<class T>
  bool First( RaiMsg *msg,  const char *fname,
              T &val ) {
    if ( this->First( msg ) && this->isNamed( fname ) ) {
      this->Get( val );
      return true;
    }
    return false;
  }
  /* return true and get value if has next field and it's named fname */
  template<class T>
  bool Next( const char *fname,  T &val ) {
    if ( this->Next() && this->isNamed( fname ) ) {
      this->Get( val );
      return true;
    }
    return false;
  }
  RaiMsg_data RawData( RaiMsg_size *dataLen = NULL ) const {
    if ( dataLen != NULL )
      *dataLen = this->fieldEnd - this->fieldStart;
    return &this->iterMsg->msgBuf[ this->fieldStart ];
  }
  void GetPointer( RaiMsg_size &fieldStart ) {
    fieldStart = this->fieldStart;
  }
  void SetPointer( RaiMsg *msg,  RaiMsg_size fieldStart )
;
  /* format message to stream, default is 'PrintTib' format */
  void Print( rai::OutputStream *output_file,
              Rai_u32 field_newlines       = 1,
              const char  * fname_format   = "%-14s : ",
              Rai_u32 print_opaques        = 1,
              const char  * debug_format   = "%-7s %3d : ",
              const char  * debug_hformat  = NULL );

  void PrintXML( rai::OutputStream *output_file,
                 Rai_u32 attr_flags = 0,
                 Rai_u32 field_newlines = 1 );
  /* return true when mem region intersects field buffer */
  bool Overlaps( const RaiMsg_data p2,  RaiMsg_size s2 ) const;
  /* return true when field is in the message buffer */
  bool Overlaps( const RaiMsg &msg ) const {
    if ( msg.msgBuf == NULL )
      return false;
    return this->Overlaps( msg.msgBuf, msg.msgSize );
  }
  /* Size of buffer needed for copy */
  RaiMsg_size CopySize( const RaiMsg_dict *entry = NULL );
  /* Copy field to toFld and use temporary buffer for field data.  If bufSize
   * bytes are zero, malloc() memory and return ptr, otherwise use buf memory
   * and return NULL. */
  RaiMsg_data CopyTo( RaiField &toFld,  Rai_u8 *buf = NULL,
                      RaiMsg_size bufSize = 0,
                      const RaiMsg_dict *entry = NULL )
;
  /* Copy field to new field, use static buffer if bufSize != 0, buf should
   * be aligned so that "(RaiField *) buf" does not cause unaligned memory
   * fault.  BufSize should be sizeof( RaiField ) + field.CopySize() */
  RaiField *Copy( Rai_u8 *buf = NULL,  RaiMsg_size bufSize = 0,
                  const RaiMsg_dict *entry = NULL );
  /* Copy field to new field, like other CopyTo() except size boundary is not
   * checked.  For use with CopySize() to allocate buffer or determine static
   * buffer is big enough ( sizeof( buf ) >= field.CopySize() ) */
  RaiField *CopyTo( RaiField &toFld,  Rai_u8 *buf,
                    const RaiMsg_dict *entry = NULL );
};


#endif
