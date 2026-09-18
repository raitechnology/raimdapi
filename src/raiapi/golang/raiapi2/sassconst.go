/* Copyright (c) 2026 Rai Technology.  All rights reserved.
 *  http://www.raitechnology.com
 * Mirrors com.rai.raimsg.SassConst: the MSG_TYPE and REC_STATUS constants used
 * by applications which deal with SASS style messages, with the string
 * conversions.  See the Java source for the full documentation of each. */
package raiapi2

import "strconv"

const (
	MDSS_CHANNEL                      = 0x12
	MSA_CHANNEL                       = 0x13
	SASS_CHANNEL                      = 0x14
	TIC_CHANNEL                       = 0x15
	SASS_TOKEN                        = 0x11
	TIC_TOKEN                         = 0x15
	MSA_TOKEN                         = 0x27
	SASS_WILDCARD                     = 0x3fffff
	MAX_SUBJECT_LEN                   = ((255*4 + 9) + 7) &^ 7
	MAX_RV_SEGMENTS                   = 32
	SASS3_SUB_MAGIC             int16 = 23176
	SASS3_PUB_MAGIC             int16 = 23177
	SNAPSHOT_FLAG                     = 0x01
	SUBSCRIBE_FLAG                    = 0x02
	INITIAL_VALUES_FLAG               = 0x04
	UNSUBSCRIBE_FLAG                  = 0x08
	REFRESH_FLAG                      = 0x10
	RESUBSCRIBE_FLAG                  = 0x80
	ENTITLED_FLAG                     = 0x4000
	IND_NONE                          = 0
	IND_UPDATE                        = 0x01
	IND_INITIAL                       = 0x02
	IND_OOB                           = 0x04
	IND_RESET                         = 0x08
	IND_SNAPSHOT                      = 0x10
	IND_ACK                           = 0x20
	VERIFY                      int16 = 0
	UPDATE                      int16 = 1
	CORRECT                     int16 = 2
	CLOSING                     int16 = 3
	DROP                        int16 = 4
	AGGREGATE                   int16 = 5
	STATUS                      int16 = 6
	CANCEL                      int16 = 7
	INITIAL                     int16 = 8
	TRANSIENT                   int16 = 9
	DERIVED                     int16 = 10
	DELETE                      int16 = 11
	SUBREINIT                   int16 = 12
	SNAPSHOT                    int16 = 13
	CONFIRM                     int16 = 14
	BDS_CONFIRM                 int16 = 15
	EDIT                        int16 = 16
	EDIT_FORCE                  int16 = 17
	RENAME                      int16 = 18
	SERVICE_STATUS              int16 = 19
	CONTRIB_REPLY               int16 = 20
	GROUP_STATUS                int16 = 21
	GROUP_MERGE                 int16 = 22
	GROUP_CHANGE                int16 = 23
	INITIAL_PASS_THRU           int16 = 24
	UPDATE_PASS_THRU            int16 = 25
	INITIAL_AGGREGATE           int16 = 26
	UPDATE_AGGREGATE            int16 = 27
	FINISH_AGGREGATE            int16 = 28
	NO_TYPE                     int16 = 0x7fff
	MAX_TYPE                    int16 = -1
	STATUS_OK                   int16 = 0
	STATUS_BAD_NAME             int16 = 1
	STATUS_BAD_LINE             int16 = 2
	STATUS_CACHE_FULL           int16 = 3
	STATUS_PERMISSION_DENIED    int16 = 4
	STATUS_PREEMPTED            int16 = 5
	STATUS_BAD_ACCESS           int16 = 6
	STATUS_TEMP_UNAVAIL         int16 = 7
	STATUS_REASSIGN             int16 = 8
	STATUS_NOSUBSCRIBERS        int16 = 9
	STATUS_EXPIRED              int16 = 10
	STATUS_TIC_DOWN             int16 = 11
	STATUS_FEED_DOWN            int16 = 12
	STATUS_GSM_DOWN             int16 = 14
	STATUS_SUBSC_DENIED         int16 = 15
	STATUS_SUBSC_TEMP_DENIED    int16 = 16
	STATUS_NOT_FOUND            int16 = 17
	STATUS_STALE_VALUE          int16 = 18
	STATUS_RELOCATE             int16 = 19
	STATUS_ENTITLEMENT_DENIED   int16 = 20
	STATUS_REC_OVERFLOW         int16 = 21
	STATUS_TIC_TUPLE_FAIL       int16 = 22
	STATUS_ENTITLEMENT_MIGRATED int16 = 23
	STATUS_CI_DISCONNECTED      int16 = 24
	STATUS_CI_DIAG_START        int16 = 25
	STATUS_NO_CACHED_DATA       int16 = 26
	STATUS_NO_REPLY             int16 = 27
	STATUS_TMF_DOWN             int16 = 28
	STATUS_TPT_DISCONNECTED     int16 = 29
	STATUS_TIMEOUT              int16 = 30
	STATUS_PERIODIC_SNAPSHOT    int16 = 64
	STATUS_FEED_UP              int16 = 65
	STATUS_HL_ROUTER_DOWN       int16 = 66
	STATUS_DQA_SUSPECT          int16 = 67
	STATUS_DQA_ACTIVE           int16 = 68
	STATUS_GSM_UP               int16 = 69
	STATUS_HL_ROUTER_UP         int16 = 71
	STATUS_TIC_UP               int16 = 72
	STATUS_FEED_SWITCHOVER      int16 = 73
	STATUS_DATA_SUSPECT         int16 = 74
	STATUS_RECAP                int16 = 75
	STATUS_CI_RECONNECTED       int16 = 76
	STATUS_CI_DIAG_END          int16 = 77
	STATUS_RECOVER_SUBSC_DENIED int16 = 80
	STATUS_CONTRIB_ACK          int16 = 81
	STATUS_CONTRIB_NACK         int16 = 82
	STATUS_TMF_UP               int16 = 83
	STATUS_TPT_CONNECTED        int16 = 84
	STATUS_FEED_NOT_ACCEPTING   int16 = 85
	MAX_STATUS                  int16 = -1
)

var MsgTypeStrings = []string{
	"VERIFY",            /* 0 */
	"UPDATE",            /* 1 */
	"CORRECT",           /* 2 */
	"CLOSING",           /* 3 */
	"DROP",              /* 4 */
	"AGGREGATE",         /* 5 */
	"STATUS",            /* 6 */
	"CANCEL",            /* 7 */
	"INITIAL",           /* 8 */
	"TRANSIENT",         /* 9 */
	"DERIVED",           /* 10 */
	"DELETE",            /* 11 */
	"SUBREINIT",         /* 12 */
	"SNAPSHOT",          /* 13 */
	"CONFIRM",           /* 14 */
	"BDS_CONFIRM",       /* 15 */
	"EDIT",              /* 16 */
	"EDIT_FORCE",        /* 17 */
	"RENAME",            /* 18 */
	"SERVICE_STATUS",    /* 19 */
	"CONTRIB_REPLY",     /* 20 */
	"GROUP_STATUS",      /* 21 */
	"GROUP_MERGE",       /* 22 */
	"GROUP_CHANGE",      /* 23 */
	"INITIAL_PASS_THRU", /* 24 */
	"UPDATE_PASS_THRU",  /* 25 */
	"INITIAL_AGGREGATE", /* 26 */
	"UPDATE_AGGREGATE",  /* 27 */
	"FINISH_AGGREGATE",  /* 28 */
}

var RecStatusStrings = []string{
	"OK",                /* 0 */
	"BAD_NAME",          /* 1 */
	"BAD_LINE",          /* 2 */
	"CACHE_FULL",        /* 3 */
	"PERMISSION_DENIED", /* 4 */
	"PREEMPTED",         /* 5 */
	"BAD_ACCESS",        /* 6 */
	"TEMP_UNAVAIL",      /* 7 */
	"REASSIGN",          /* 8 */
	"NOSUBSCRIBERS",     /* 9 */
	"EXPIRED",           /* 10 */
	"TIC_DOWN",          /* 11 */
	"FEED_DOWN",         /* 12 */
	"13",
	"GSM_DOWN",             /* 14 */
	"SUBSC_DENIED",         /* 15 */
	"SUBSC_TEMP_DENIED",    /* 16 */
	"NOT_FOUND",            /* 17 */
	"STALE_VALUE",          /* 18 */
	"RELOCATE",             /* 19 */
	"ENTITLEMENT_DENIED",   /* 20 */
	"REC_OVERFLOW",         /* 21 */
	"TIC_TUPLE_FAIL",       /* 22 */
	"ENTITLEMENT_MIGRATED", /* 23 */
	"CI_DISCONNECTED",      /* 24 */
	"CI_DIAG_START",        /* 25 */
	"NO_CACHED_DATA",       /* 26 */
	"NO_REPLY",             /* 27 */
	"TMF_DOWN",             /* 28 */
	"TPT_DISCONNECTED",     /* 29 */
	"TIMEOUT",              /* 30 */
	"31", "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42",
	"43", "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54",
	"55", "56", "57", "58", "59", "60", "61", "62", "63",
	"PERIODIC_SNAPSHOT", /* 64 */
	"FEED_UP",           /* 65 */
	"HL_ROUTER_DOWN",    /* 66 */
	"DQA_SUSPECT",       /* 67 */
	"DQA_ACTIVE",        /* 68 */
	"GSM_UP",            /* 69 */
	"70",
	"HL_ROUTER_UP",    /* 71 */
	"TIC_UP",          /* 72 */
	"FEED_SWITCHOVER", /* 73 */
	"DATA_SUSPECT",    /* 74 */
	"RECAP",           /* 75 */
	"CI_RECONNECTED",  /* 76 */
	"CI_DIAG_END",     /* 77 */
	"78", "79",
	"RECOVER_SUBSC_DENIED", /* 80 */
	"CONTRIB_ACK",          /* 81 */
	"CONTRIB_NACK",         /* 82 */
	"TMF_UP",               /* 83 */
	"TPT_CONNECTED",        /* 84 */
	"FEED_NOT_ACCEPTING",   /* 85 */
}

/* SassMsgTypeToString converts a message type to a string (8 = "INITIAL"),
 * or the number when not found */
func SassMsgTypeToString(msgType int16) string {
	if msgType < 0 || int(msgType) >= len(MsgTypeStrings) {
		return strconv.Itoa(int(msgType))
	}
	return MsgTypeStrings[msgType]
}

/* SassStringToMsgType converts a string to a message type ("INITIAL" = 8),
 * MAX_TYPE when not found */
func SassStringToMsgType(s string) int16 {
	for i, t := range MsgTypeStrings {
		if s == t {
			return int16(i)
		}
	}
	return MAX_TYPE
}

/* SassRecStatusToString converts a rec status to a string (0 = "OK") */
func SassRecStatusToString(recStatus int16) string {
	if recStatus < 0 || int(recStatus) >= len(RecStatusStrings) {
		return strconv.Itoa(int(recStatus))
	}
	return RecStatusStrings[recStatus]
}

/* SassStringToRecStatus converts a string to a rec status, MAX_STATUS when
 * not found */
func SassStringToRecStatus(s string) int16 {
	for i, t := range RecStatusStrings {
		if s == t {
			return int16(i)
		}
	}
	return MAX_STATUS
}
