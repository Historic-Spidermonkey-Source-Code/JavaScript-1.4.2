/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "NPL"); you may not use this file except in
 * compliance with the NPL.  You may obtain a copy of the NPL at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the NPL is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the NPL
 * for the specific language governing rights and limitations under the
 * NPL.
 *
 * The Initial Developer of this code under the NPL is Netscape
 * Communications Corporation.  Portions created by Netscape are
 * Copyright (C) 1998 Netscape Communications Corporation.  All Rights
 * Reserved.
 */

/*
 * PR time code.
 */
#ifdef SOLARIS
#define _REENTRANT 1
#endif
#include <string.h>
#include <time.h>
#include "jstypes.h"

#include "jsprf.h"
#include "prmjtime.h"

#define PRMJ_DO_MILLISECONDS 1

#ifdef XP_PC
#include <sys/timeb.h>
#endif

#ifdef XP_MAC
#include <OSUtils.h>
#include <TextUtils.h>
#include <Resources.h>
#include <Timer.h>
#endif

#ifdef XP_UNIX

#ifdef _SVID_GETTOD   /* Defined only on Solaris, see Solaris <sys/types.h> */
extern int gettimeofday(struct timeval *tv);
#endif

#include <sys/time.h>

#endif /* XP_UNIX */

#ifdef XP_MAC
static UnsignedWide		dstLocalBaseMicroseconds;
static unsigned long	gJanuaryFirst1970Seconds;

static void MacintoshInitializeTime(void)
{
	UnsignedWide			upTime;
	unsigned long			currentLocalTimeSeconds,
							startupTimeSeconds;
	uint64					startupTimeMicroSeconds;
	uint32					upTimeSeconds;
	uint64					oneMillion, upTimeSecondsLong, microSecondsToSeconds;
	DateTimeRec				firstSecondOfUnixTime;

	//	Figure out in local time what time the machine
	//	started up.  This information can be added to
	//	upTime to figure out the current local time
	//	as well as GMT.

	Microseconds(&upTime);

	GetDateTime(&currentLocalTimeSeconds);

	JSLL_I2L(microSecondsToSeconds, PRMJ_USEC_PER_SEC);
	JSLL_DIV(upTimeSecondsLong,  *((uint64 *)&upTime), microSecondsToSeconds);
	JSLL_L2I(upTimeSeconds, upTimeSecondsLong);

	startupTimeSeconds = currentLocalTimeSeconds - upTimeSeconds;

	//	Make sure that we normalize the macintosh base seconds
	//	to the unix base of January 1, 1970.

	firstSecondOfUnixTime.year = 1970;
	firstSecondOfUnixTime.month = 1;
	firstSecondOfUnixTime.day = 1;
	firstSecondOfUnixTime.hour = 0;
	firstSecondOfUnixTime.minute = 0;
	firstSecondOfUnixTime.second = 0;
	firstSecondOfUnixTime.dayOfWeek = 0;

	DateToSeconds(&firstSecondOfUnixTime, &gJanuaryFirst1970Seconds);

	startupTimeSeconds -= gJanuaryFirst1970Seconds;

	//	Now convert the startup time into a wide so that we
	//	can figure out GMT and DST.

	JSLL_I2L(startupTimeMicroSeconds, startupTimeSeconds);
	JSLL_I2L(oneMillion, PRMJ_USEC_PER_SEC);
	JSLL_MUL(dstLocalBaseMicroseconds, oneMillion, startupTimeMicroSeconds);
}

// Because serial port and SLIP conflict with ReadXPram calls,
// we cache the call here

static void MyReadLocation(MachineLocation * loc)
{
	static MachineLocation storedLoc;	// InsideMac, OSUtilities, page 4-20
	static JSBool didReadLocation = JS_FALSE;
	if (!didReadLocation)
	{
		MacintoshInitializeTime();
		ReadLocation(&storedLoc);
		didReadLocation = JS_TRUE;
	}
	*loc = storedLoc;
}
#endif /* XP_MAC */

#define IS_LEAP(year) \
   (year != 0 && ((((year & 0x3) == 0) &&  \
		   ((year - ((year/100) * 100)) != 0)) || \
		  (year - ((year/400) * 400)) == 0))

#define PRMJ_HOUR_SECONDS  3600L
#define PRMJ_DAY_SECONDS  (24L * PRMJ_HOUR_SECONDS)
#define PRMJ_YEAR_SECONDS (PRMJ_DAY_SECONDS * 365L)
#define PRMJ_MAX_UNIX_TIMET 2145859200L /*time_t value equiv. to 12/31/2037 */
/* function prototypes */
static void PRMJ_basetime(JSInt64 tsecs, PRMJTime *prtm);
/*
 * get the difference in seconds between this time zone and UTC (GMT)
 */
time_t
PRMJ_LocalGMTDifference()
{
#if defined(XP_UNIX) || defined(XP_PC)
    struct tm ltime;

    /* get the difference between this time zone and GMT */
    memset((char *)&ltime,0,sizeof(ltime));
    ltime.tm_mday = 2;
    ltime.tm_year = 70;
#ifdef SUNOS4
    ltime.tm_zone = 0;
    ltime.tm_gmtoff = 0;
    return timelocal(&ltime) - (24 * 3600);
#else
    return mktime(&ltime) - (24L * 3600L);
#endif
#endif
#if defined(XP_MAC)
    static time_t    zone = -1L;
    MachineLocation  machineLocation;
    JSUint64	     gmtOffsetSeconds;
    JSUint64	     gmtDelta;
    JSUint64	     dlsOffset;
    JSInt32	     offset;

    /* difference has been set no need to recalculate */
    if(zone != -1)
	return zone;

    /* Get the information about the local machine, including
     * its GMT offset and its daylight savings time info.
     * Convert each into wides that we can add to
     * startupTimeMicroSeconds.
     */

    MyReadLocation(&machineLocation);

    /* Mask off top eight bits of gmtDelta, sign extend lower three. */

    if ((machineLocation.u.gmtDelta & 0x00800000) != 0) {
	gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF) | 0xFF000000;
	gmtOffsetSeconds.hi = 0xFFFFFFFF;
	JSLL_UI2L(gmtDelta,0);
    } else {
	gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF);
	gmtOffsetSeconds.hi = 0;
	JSLL_UI2L(gmtDelta,PRMJ_DAY_SECONDS);
    }

    /*
     * Normalize time to be positive if you are behind GMT. gmtDelta will
     * always be positive.
     */
    JSLL_SUB(gmtDelta,gmtDelta,gmtOffsetSeconds);

    /* Is Daylight Savings On?  If so, we need to add an hour to the offset. */
    if (machineLocation.u.dlsDelta != 0) {
	JSLL_UI2L(dlsOffset, PRMJ_HOUR_SECONDS);
    } else {
	JSLL_I2L(dlsOffset, 0);
    }

    JSLL_ADD(gmtDelta,gmtDelta, dlsOffset);
    JSLL_L2I(offset,gmtDelta);

    zone = offset;
    return (time_t)offset;
#endif
}

/* Constants for GMT offset from 1970 */
#define G1970GMTMICROHI        0x00dcdcad /* micro secs to 1970 hi */
#define G1970GMTMICROLOW       0x8b3fa000 /* micro secs to 1970 low */

#define G2037GMTMICROHI        0x00e45fab /* micro secs to 2037 high */
#define G2037GMTMICROLOW       0x7a238000 /* micro secs to 2037 low */

/* Convert from base time to extended time */
static JSInt64
PRMJ_ToExtendedTime(JSInt32 time)
{
    JSInt64 exttime;
    JSInt64 g1970GMTMicroSeconds;
    JSInt64 low;
    time_t diff;
    JSInt64  tmp;
    JSInt64  tmp1;

    diff = PRMJ_LocalGMTDifference();
    JSLL_UI2L(tmp, PRMJ_USEC_PER_SEC);
    JSLL_I2L(tmp1,diff);
    JSLL_MUL(tmp,tmp,tmp1);

    JSLL_UI2L(g1970GMTMicroSeconds,G1970GMTMICROHI);
    JSLL_UI2L(low,G1970GMTMICROLOW);
#ifndef JS_HAVE_LONG_LONG
    JSLL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
    JSLL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
#else
    JSLL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,32);
#endif
    JSLL_ADD(g1970GMTMicroSeconds,g1970GMTMicroSeconds,low);

    JSLL_I2L(exttime,time);
    JSLL_ADD(exttime,exttime,g1970GMTMicroSeconds);
    JSLL_SUB(exttime,exttime,tmp);
    return exttime;
}

JSInt64
PRMJ_Now(void)
{
#ifdef XP_PC
    JSInt64 s, us, ms2us, s2us;
    struct timeb b;
#endif /* XP_PC */
#ifdef XP_UNIX
    struct timeval tv;
    JSInt64 s, us, s2us;
#endif /* XP_UNIX */
#ifdef XP_MAC
    UnsignedWide upTime;
    JSInt64	 localTime;
    JSInt64       gmtOffset;
    JSInt64    dstOffset;
    time_t       gmtDiff;
    JSInt64	 s2us;
#endif /* XP_MAC */

#ifdef XP_PC
    ftime(&b);
    JSLL_UI2L(ms2us, PRMJ_USEC_PER_MSEC);
    JSLL_UI2L(s2us, PRMJ_USEC_PER_SEC);
    JSLL_UI2L(s, b.time);
    JSLL_UI2L(us, b.millitm);
    JSLL_MUL(us, us, ms2us);
    JSLL_MUL(s, s, s2us);
    JSLL_ADD(s, s, us);
    return s;
#endif

#ifdef XP_UNIX
#ifdef _SVID_GETTOD   /* Defined only on Solaris, see Solaris <sys/types.h> */
    gettimeofday(&tv);
#else
    gettimeofday(&tv, 0);
#endif /* _SVID_GETTOD */
    JSLL_UI2L(s2us, PRMJ_USEC_PER_SEC);
    JSLL_UI2L(s, tv.tv_sec);
    JSLL_UI2L(us, tv.tv_usec);
    JSLL_MUL(s, s, s2us);
    JSLL_ADD(s, s, us);
    return s;
#endif /* XP_UNIX */
#ifdef XP_MAC
    JSLL_UI2L(localTime,0);
    gmtDiff = PRMJ_LocalGMTDifference();
    JSLL_I2L(gmtOffset,gmtDiff);
    JSLL_UI2L(s2us, PRMJ_USEC_PER_SEC);
    JSLL_MUL(gmtOffset,gmtOffset,s2us);
    JSLL_UI2L(dstOffset,0);
    dstOffset = PRMJ_DSTOffset(dstOffset);
    JSLL_SUB(gmtOffset,gmtOffset,dstOffset);
    /* don't adjust for DST since it sets ctime and gmtime off on the MAC */
    Microseconds(&upTime);
    JSLL_ADD(localTime,localTime,gmtOffset);
    JSLL_ADD(localTime,localTime, *((JSUint64 *)&dstLocalBaseMicroseconds));
    JSLL_ADD(localTime,localTime, *((JSUint64 *)&upTime));

    return *((JSUint64 *)&localTime);
#endif /* XP_MAC */
}

/* Get the DST timezone offset for the time passed in */
JSInt64
PRMJ_DSTOffset(JSInt64 time)
{
    JSInt64 us2s;
#ifdef XP_MAC
    MachineLocation  machineLocation;
    JSInt64 dlsOffset;
    /*	Get the information about the local machine, including
     *	its GMT offset and its daylight savings time info.
     *	Convert each into wides that we can add to
     *	startupTimeMicroSeconds.
     */
    MyReadLocation(&machineLocation);

    /* Is Daylight Savings On?  If so, we need to add an hour to the offset. */
    if (machineLocation.u.dlsDelta != 0) {
	JSLL_UI2L(us2s, PRMJ_USEC_PER_SEC); /* seconds in a microseconds */
	JSLL_UI2L(dlsOffset, PRMJ_HOUR_SECONDS);  /* seconds in one hour       */
	JSLL_MUL(dlsOffset, dlsOffset, us2s);
    } else {
	JSLL_I2L(dlsOffset, 0);
    }
    return(dlsOffset);
#else
    time_t local;
    JSInt32 diff;
    JSInt64  maxtimet;
    struct tm tm;
    PRMJTime prtm;
#if defined( XP_PC ) || defined( FREEBSD ) || defined ( HPUX9 ) || defined ( SNI ) || defined ( NETBSD ) || defined ( OPENBSD ) || defined( RHAPSODY )
    struct tm *ptm;
#endif


    JSLL_UI2L(us2s, PRMJ_USEC_PER_SEC);
    JSLL_DIV(time, time, us2s);

    /* get the maximum of time_t value */
    JSLL_UI2L(maxtimet,PRMJ_MAX_UNIX_TIMET);

    if(JSLL_CMP(time,>,maxtimet)){
      JSLL_UI2L(time,PRMJ_MAX_UNIX_TIMET);
    } else if(!JSLL_GE_ZERO(time)){
      /*go ahead a day to make localtime work (does not work with 0) */
      JSLL_UI2L(time,PRMJ_DAY_SECONDS);
    }
    JSLL_L2UI(local,time);
    PRMJ_basetime(time,&prtm);
#if defined( XP_PC ) || defined( FREEBSD ) || defined ( HPUX9 ) || defined ( SNI ) || defined ( NETBSD ) || defined ( OPENBSD ) || defined( RHAPSODY )
    ptm = localtime(&local);
    if(!ptm){
      return JSLL_ZERO;
    }
    tm = *ptm;
#else
    localtime_r(&local,&tm); /* get dst information */
#endif

    diff = ((tm.tm_hour - prtm.tm_hour) * PRMJ_HOUR_SECONDS) +
	((tm.tm_min - prtm.tm_min) * 60);

    if(diff < 0){
	diff += PRMJ_DAY_SECONDS;
    }

    JSLL_UI2L(time,diff);

    JSLL_MUL(time,time,us2s);

    return(time);
#endif
}

/* Format a time value into a buffer. Same semantics as strftime() */
size_t
PRMJ_FormatTime(char *buf, int buflen, char *fmt, PRMJTime *prtm)
{
#if defined(XP_UNIX) || defined(XP_PC) || defined(XP_MAC)
    struct tm a;

    /* Zero out the tm struct.  Linux, SunOS 4 struct tm has extra members int
     * tm_gmtoff, char *tm_zone; when tm_zone is garbage, strftime gets
     * confused and dumps core.  NSPR20 prtime.c attempts to fill these in by
     * calling mktime on the partially filled struct, but this doesn't seem to
     * work as well; the result string has "can't get timezone" for ECMA-valid
     * years.  Might still make sense to use this, but find the range of years
     * for which valid tz information exists, and map (per ECMA hint) from the
     * given year into that range.
     
     * N.B. This hasn't been tested with anything that actually _uses_
     * tm_gmtoff; zero might be the wrong thing to set it to if you really need
     * to format a time.  This fix is for jsdate.c, which only uses
     * JS_FormatTime to get a string representing the time zone.  */
    memset(&a, 0, sizeof(struct tm));

    a.tm_sec = prtm->tm_sec;
    a.tm_min = prtm->tm_min;
    a.tm_hour = prtm->tm_hour;
    a.tm_mday = prtm->tm_mday;
    a.tm_mon = prtm->tm_mon;
    a.tm_wday = prtm->tm_wday;
    a.tm_year = prtm->tm_year - 1900;
    a.tm_yday = prtm->tm_yday;
    a.tm_isdst = prtm->tm_isdst;

    /* Even with the above, SunOS 4 seems to detonate if tm_zone and tm_gmtoff
     * are null.  This doesn't quite work, though - the timezone is off by
     * tzoff + dst.  (And mktime seems to return -1 for the exact dst
     * changeover time.)

     * Still not sure if MKLINUX is necessary; this is borrowed from the NSPR20
     * prtime.c.  I'm leaving it out - My Linux does the right thing without it
     * (and the wrong thing with it) even though it has the tm_gmtoff, tm_zone
     * fields.  Linux seems to be happy so long as the tm struct is zeroed out.
     * The #ifdef in nspr is:
     * #if defined(SUNOS4) || defined(MKLINUX) || defined (__GLIBC >= 2)
     */

#if defined(SUNOS4)
    if (mktime(&a) == -1) {
        /* Seems to fail whenever the requested date is outside of the 32-bit
         * UNIX epoch.  We could proceed at this point (setting a.tm_zone to
         * "") but then strftime returns a string with a 2-digit field of
         * garbage for the year.  So we return 0 and hope jsdate.c
         * will fall back on toString.
         */
        return 0;
    }
#endif

    return strftime(buf, buflen, fmt, &a);
#endif
}

/* table for number of days in a month */
static int mtab[] = {
  /* jan, feb,mar,apr,may,jun */
  31,28,31,30,31,30,
  /* july,aug,sep,oct,nov,dec */
  31,31,30,31,30,31
};

/*
 * basic time calculation functionality for localtime and gmtime
 * setups up prtm argument with correct values based upon input number
 * of seconds.
 */
static void
PRMJ_basetime(JSInt64 tsecs, PRMJTime *prtm)
{
    /* convert tsecs back to year,month,day,hour,secs */
    JSInt32 year    = 0;
    JSInt32 month   = 0;
    JSInt32 yday    = 0;
    JSInt32 mday    = 0;
    JSInt32 wday    = 6; /* start on a Sunday */
    JSInt32 days    = 0;
    JSInt32 seconds = 0;
    JSInt32 minutes = 0;
    JSInt32 hours   = 0;
    JSInt32 isleap  = 0;
    JSInt64 result;
    JSInt64	result1;
    JSInt64	result2;
    JSInt64 base;

    JSLL_UI2L(result,0);
    JSLL_UI2L(result1,0);
    JSLL_UI2L(result2,0);

    /* get the base time via UTC */
    base = PRMJ_ToExtendedTime(0);
    JSLL_UI2L(result,  PRMJ_USEC_PER_SEC);
    JSLL_DIV(base,base,result);
    JSLL_ADD(tsecs,tsecs,base);

    JSLL_UI2L(result, PRMJ_YEAR_SECONDS);
    JSLL_UI2L(result1,PRMJ_DAY_SECONDS);
    JSLL_ADD(result2,result,result1);

  /* get the year */
    while ((isleap == 0) ? !JSLL_CMP(tsecs,<,result) : !JSLL_CMP(tsecs,<,result2)) {
	/* subtract a year from tsecs */
	JSLL_SUB(tsecs,tsecs,result);
	days += 365;
	/* is it a leap year ? */
	if(IS_LEAP(year)){
	    JSLL_SUB(tsecs,tsecs,result1);
	    days++;
	}
	year++;
	isleap = IS_LEAP(year);
    }

    JSLL_UI2L(result1,PRMJ_DAY_SECONDS);

    JSLL_DIV(result,tsecs,result1);
    JSLL_L2I(mday,result);

  /* let's find the month */
    while(((month == 1 && isleap) ?
	   (mday >= mtab[month] + 1) :
	   (mday >= mtab[month]))){
	yday += mtab[month];
	days += mtab[month];

	mday -= mtab[month];

    /* it's a Feb, check if this is a leap year */
	if(month == 1 && isleap != 0){
	    yday++;
	    days++;
	    mday--;
	}
	month++;
    }

    /* now adjust tsecs */
    JSLL_MUL(result,result,result1);
    JSLL_SUB(tsecs,tsecs,result);

    mday++; /* day of month always start with 1 */
    days += mday;
    wday = (days + wday) % 7;

    yday += mday;

    /* get the hours */
    JSLL_UI2L(result1,PRMJ_HOUR_SECONDS);
    JSLL_DIV(result,tsecs,result1);
    JSLL_L2I(hours,result);
    JSLL_MUL(result,result,result1);
    JSLL_SUB(tsecs,tsecs,result);

    /* get minutes */
    JSLL_UI2L(result1,60);
    JSLL_DIV(result,tsecs,result1);
    JSLL_L2I(minutes,result);
    JSLL_MUL(result,result,result1);
    JSLL_SUB(tsecs,tsecs,result);

    JSLL_L2I(seconds,tsecs);

    prtm->tm_usec  = 0L;
    prtm->tm_sec   = (JSInt8)seconds;
    prtm->tm_min   = (JSInt8)minutes;
    prtm->tm_hour  = (JSInt8)hours;
    prtm->tm_mday  = (JSInt8)mday;
    prtm->tm_mon   = (JSInt8)month;
    prtm->tm_wday  = (JSInt8)wday;
    prtm->tm_year  = (JSInt16)year;
    prtm->tm_yday  = (JSInt16)yday;
}
