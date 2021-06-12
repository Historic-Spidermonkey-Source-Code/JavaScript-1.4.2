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
 * By default all math calls go to fdlibm.  The defines for each platform
 * remap the math calls to native routines.
 */

#ifndef _LIBMATH_H
#define _LIBMATH_H

#include <math.h>
#include "jsconfig.h"

/*
 * Define which platforms on which to use fdlibm.  Not used
 * by default since there can be problems with endian-ness and such.
 */

#if defined(_WIN32)
#define JS_USE_FDLIBM_MATH 1

#elif defined(SUNOS4)
#define JS_USE_FDLIBM_MATH 1

#elif defined(IRIX)
#define JS_USE_FDLIBM_MATH 1

#elif defined(SOLARIS)
#define JS_USE_FDLIBM_MATH 1

#elif defined(HPUX)
#define JS_USE_FDLIBM_MATH 1

#elif defined(linux)
#define JS_USE_FDLIBM_MATH 1

#elif defined(OSF1)
/* Want to use some fdlibm functions but fdlibm broken on OSF1/alpha. */
#define JS_USE_FDLIBM_MATH 0

#elif defined(AIX)
#define JS_USE_FDLIBM_MATH 1

#else
#define JS_USE_FDLIBM_MATH 0
#endif

#if !JS_USE_FDLIBM_MATH

/*
 * Use system provided math routines.
 */

#define fd_acos acos
#define fd_asin asin
#define fd_atan atan
#define fd_atan2 atan2
#define fd_ceil ceil
#define fd_copysign copysign
#define fd_cos cos
#define fd_exp exp
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod
#define fd_log log
#define fd_pow pow
#define fd_sin sin
#define fd_sqrt sqrt
#define fd_tan tan

#else

/*
 * Use math routines in fdlibm.
 */

#ifdef __STDC__
#define __P(p)  p
#else
#define __P(p)  ()
#endif

#if defined _WIN32 || defined SUNOS4 

#define fd_acos acos
#define fd_asin asin
#define fd_atan atan
#define fd_cos cos
#define fd_sin sin
#define fd_tan tan
#define fd_exp exp
#define fd_log log
#define fd_sqrt sqrt
#define fd_ceil ceil
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod

extern double fd_atan2 __P((double, double));
extern double fd_copysign __P((double, double));

#ifdef DEBUG
extern double fd_pow __P((double, double));
#else
#define fd_pow pow
#endif

#elif defined IRIX

#define fd_acos acos
#define fd_asin asin
#define fd_atan atan
#define fd_exp exp
#define fd_log log
#define fd_log10 log10
#define fd_sqrt sqrt
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod

extern double fd_cos __P((double));
extern double fd_sin __P((double));
extern double fd_tan __P((double));
extern double fd_atan2 __P((double, double));
extern double fd_pow __P((double, double));
extern double fd_ceil __P((double));
extern double fd_copysign __P((double, double));

#elif defined SOLARIS

#define fd_atan atan
#define fd_cos cos
#define fd_sin sin
#define fd_tan tan
#define fd_exp exp
#define fd_sqrt sqrt
#define fd_ceil ceil
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod

extern double fd_acos __P((double));
extern double fd_asin __P((double));
extern double fd_log __P((double));
extern double fd_atan2 __P((double, double));
extern double fd_pow __P((double, double));
extern double fd_copysign __P((double, double));

#elif defined HPUX

#define fd_cos cos
#define fd_sin sin
#define fd_exp exp
#define fd_sqrt sqrt
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod

extern double fd_ceil __P((double));
extern double fd_acos __P((double));
extern double fd_log __P((double));
extern double fd_atan2 __P((double, double));
extern double fd_tan __P((double));
extern double fd_pow __P((double, double));
extern double fd_asin __P((double));
extern double fd_atan __P((double));
extern double fd_copysign __P((double, double));

#elif defined(linux)

#define fd_atan atan
#define fd_atan2 atan2
#define fd_ceil ceil
#define fd_cos cos
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod
#define fd_sin sin
#define fd_sqrt sqrt
#define fd_tan tan

extern double fd_asin __P((double));
extern double fd_acos __P((double));
extern double fd_exp __P((double));
extern double fd_log __P((double));
extern double fd_pow __P((double, double));
extern double fd_copysign __P((double, double));

#elif defined(OSF1)

#define fd_acos acos
#define fd_asin asin
#define fd_atan atan
#define fd_copysign copysign
#define fd_cos cos
#define fd_exp exp
#define fd_fabs fabs
#define fd_fmod fmod
#define fd_sin sin
#define fd_sqrt sqrt
#define fd_tan tan

extern double fd_atan2 __P((double, double));
extern double fd_ceil __P((double));
extern double fd_floor __P((double));
extern double fd_log __P((double));
extern double fd_pow __P((double, double));

#elif defined(AIX)

#define fd_acos acos
#define fd_asin asin
#define fd_atan2 atan2
#define fd_copysign copysign
#define fd_cos cos
#define fd_exp exp
#define fd_fabs fabs
#define fd_floor floor
#define fd_fmod fmod
#define fd_log log
#define fd_sin sin
#define fd_sqrt sqrt

extern double fd_atan __P((double));
extern double fd_ceil __P((double));
extern double fd_pow __P((double,double));
extern double fd_tan __P((double));

#else /* other platform.. generic paranoid slow fdlibm */

extern double fd_acos __P((double));
extern double fd_asin __P((double));
extern double fd_atan __P((double));
extern double fd_cos __P((double));
extern double fd_sin __P((double));
extern double fd_tan __P((double));
 
extern double fd_exp __P((double));
extern double fd_log __P((double));
extern double fd_sqrt __P((double));

extern double fd_ceil __P((double));
extern double fd_fabs __P((double));
extern double fd_floor __P((double));
extern double fd_fmod __P((double, double));

extern double fd_atan2 __P((double, double));
extern double fd_pow __P((double, double));
extern double fd_copysign __P((double, double));

#endif

#endif /* JS_USE_FDLIBM_MATH */

#endif /* _LIBMATH_H */

