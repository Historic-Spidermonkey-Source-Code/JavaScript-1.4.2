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
 * JS number type and wrapper class.
 */
#include "jsstddef.h"
#include <errno.h>
#ifdef XP_PC
#include <float.h>
#endif
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "jstypes.h"
#include "jsutil.h" /* Added by JSIFY */
#include "jsdtoa.h"
#include "jsprf.h"
#include "jsapi.h"
#include "jsatom.h"
#include "jscntxt.h"
#include "jsconfig.h"
#include "jsgc.h"
#include "jsinterp.h"
#include "jsnum.h"
#include "jsobj.h"
#include "jsopcode.h"
#include "jsstr.h"

union dpun {
    struct {
#ifdef IS_LITTLE_ENDIAN
	uint32 lo, hi;
#else
	uint32 hi, lo;
#endif
    } s;
    jsdouble d;
};

static JSBool
num_isNaN(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    jsdouble x;

    if (!js_ValueToNumber(cx, argv[0], &x))
	return JS_FALSE;
    *rval = BOOLEAN_TO_JSVAL(JSDOUBLE_IS_NaN(x));
    return JS_TRUE;
}

static JSBool
num_isFinite(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    jsdouble x;

    if (!js_ValueToNumber(cx, argv[0], &x))
	return JS_FALSE;
    *rval = BOOLEAN_TO_JSVAL(JSDOUBLE_IS_FINITE(x));
    return JS_TRUE;
}

static JSBool
num_parseFloat(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSString *str;
    jsdouble d;
    const jschar *ep;

    str = js_ValueToString(cx, argv[0]);
    if (!str)
	return JS_FALSE;
    if (!js_strtod(cx, str->chars, &ep, &d))
	return JS_FALSE;
    if (ep == str->chars) {
	*rval = DOUBLE_TO_JSVAL(cx->runtime->jsNaN);
	return JS_TRUE;
    }
    return js_NewNumberValue(cx, d, rval);
}

/* See ECMA 15.1.2.2. */
static JSBool
num_parseInt(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    JSString *str;
    jsint radix;
    jsdouble d;
    const jschar *ep;

    str = js_ValueToString(cx, argv[0]);
    if (!str)
	return JS_FALSE;

    if (argc > 1) {
	if (!js_ValueToECMAInt32(cx, argv[1], &radix))
	    return JS_FALSE;
    } else
	radix = 0;

    if (radix != 0 && (radix < 2 || radix > 36)) {
	*rval = DOUBLE_TO_JSVAL(cx->runtime->jsNaN);
	return JS_TRUE;
    }
    if (!js_strtointeger(cx, str->chars, &ep, radix, &d))
	return JS_FALSE;
    if (ep == str->chars) {
	*rval = DOUBLE_TO_JSVAL(cx->runtime->jsNaN);
	return JS_TRUE;
    }
    return js_NewNumberValue(cx, d, rval);
}


static JSFunctionSpec number_functions[] = {
    {"isNaN",           num_isNaN,              1},
    {"isFinite",        num_isFinite,           1},
    {"parseFloat",      num_parseFloat,         1},
    {"parseInt",        num_parseInt,           2},
    {0}
};

static JSClass number_class = {
    "Number",
    JSCLASS_HAS_PRIVATE,
    JS_PropertyStub,  JS_PropertyStub,  JS_PropertyStub,  JS_PropertyStub,
    JS_EnumerateStub, JS_ResolveStub,   JS_ConvertStub,   JS_FinalizeStub
};

static JSBool
Number(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    jsdouble d;
    jsval v;

    if (argc != 0) {
	if (!js_ValueToNumber(cx, argv[0], &d))
	    return JS_FALSE;
    } else {
	d = 0.0;
    }
    if (!js_NewNumberValue(cx, d, &v))
	return JS_FALSE;
    if (!cx->fp->constructing) {
	*rval = v;
	return JS_TRUE;
    }
    OBJ_SET_SLOT(cx, obj, JSSLOT_PRIVATE, v);
    return JS_TRUE;
}

#if JS_HAS_TOSOURCE
static JSBool
num_toSource(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    jsval v;
    jsdouble d;
    size_t i;
    char buf[64];
    JSString *str;

    if (!JS_InstanceOf(cx, obj, &number_class, argv))
	return JS_FALSE;
    v = OBJ_GET_SLOT(cx, obj, JSSLOT_PRIVATE);
    if (!JSVAL_IS_NUMBER(v))
	return js_obj_toSource(cx, obj, argc, argv, rval);
    d = JSVAL_IS_INT(v) ? (jsdouble)JSVAL_TO_INT(v) : *JSVAL_TO_DOUBLE(v);
    i = JS_snprintf(buf, sizeof buf, "(new %s(", number_class.name);

    JS_cnvtf(buf + i, sizeof buf - i, 20, d);
    i = strlen(buf);
    JS_snprintf(buf + i, sizeof buf - i, "))");
    str = JS_NewStringCopyZ(cx, buf);
    if (!str)
	return JS_FALSE;
    *rval = STRING_TO_JSVAL(str);
    return JS_TRUE;
}
#endif

static JSBool
num_toString(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    jsval v;
    jsdouble d;
    jsint base, ival, dval;
    char *bp, buf[32];
    JSString *str;

    if (!JS_InstanceOf(cx, obj, &number_class, argv))
	return JS_FALSE;
    v = OBJ_GET_SLOT(cx, obj, JSSLOT_PRIVATE);
    if (!JSVAL_IS_NUMBER(v))
	return js_obj_toString(cx, obj, argc, argv, rval);
    d = JSVAL_IS_INT(v) ? (jsdouble)JSVAL_TO_INT(v) : *JSVAL_TO_DOUBLE(v);
    if (argc != 0) {
	if (!js_ValueToECMAInt32(cx, argv[0], &base))
	    return JS_FALSE;
	if (base < 2 || base > 36) {
	    char numBuf[12];
	    JS_snprintf(numBuf, sizeof numBuf, "%ld", (long) base);
	    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_BAD_RADIX,
	    			 numBuf);
	    return JS_FALSE;
	}
	if (base != 10 && JSDOUBLE_IS_FINITE(d)) {
	    ival = (jsint) js_DoubleToInteger(d);
	    bp = buf + sizeof buf;
	    for (*--bp = '\0'; ival != 0 && --bp >= buf; ival /= base) {
		dval = ival % base;
		*bp = (char)((dval >= 10) ? 'a' - 10 + dval : '0' + dval);
	    }
	    if (*bp == '\0')
		*--bp = '0';
	    str = JS_NewStringCopyZ(cx, bp);
	} else {
	    str = js_NumberToString(cx, d);
	}
    } else {
	str = js_NumberToString(cx, d);
    }
    if (!str)
	return JS_FALSE;
    *rval = STRING_TO_JSVAL(str);
    return JS_TRUE;
}

static JSBool
num_valueOf(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval)
{
    if (!JS_InstanceOf(cx, obj, &number_class, argv))
	return JS_FALSE;
    *rval = OBJ_GET_SLOT(cx, obj, JSSLOT_PRIVATE);
    return JS_TRUE;
}

static JSFunctionSpec number_methods[] = {
#if JS_HAS_TOSOURCE
    {js_toSource_str,   num_toSource,   0},
#endif
    {js_toString_str,	num_toString,	0},
    {js_valueOf_str,	num_valueOf,	0},
    {0}
};

/* NB: Keep this in synch with number_constants[]. */
enum nc_slot {
    NC_NaN,
    NC_POSITIVE_INFINITY,
    NC_NEGATIVE_INFINITY,
    NC_MAX_VALUE,
    NC_MIN_VALUE,
    NC_LIMIT
};

/*
 * Some to most C compilers forbid spelling these at compile time, or barf
 * if you try, so all but MAX_VALUE are set at runtime by js_InitNumberClass
 * using union dpun.
 */
static JSConstDoubleSpec number_constants[] = {
    {0,                         "NaN"},
    {0,                         "POSITIVE_INFINITY"},
    {0,                         "NEGATIVE_INFINITY"},
    {1.7976931348623157E+308,   "MAX_VALUE"},
    {0,                         "MIN_VALUE"},
    {0}
};

static jsdouble NaN;

JSObject *
js_InitNumberClass(JSContext *cx, JSObject *obj)
{
    JSRuntime *rt;
    union dpun u;
    JSObject *proto, *ctor;

    rt = cx->runtime;
    if (!rt->jsNaN) {
#ifdef XP_PC
#ifdef XP_OS2
	/*DSR071597 - I have no idea what this really does other than mucking with the floating     */
	/*point unit, but it does fix a "floating point underflow" exception I am getting, and there*/
	/*is similar code in the Hursley java. Making sure we have the same code in Javascript      */
	/*where Netscape was calling control87 on Windows...                                        */
	_control87(MCW_EM+PC_53+RC_NEAR,MCW_EM+MCW_PC+MCW_RC);
#else
	_control87(MCW_EM, MCW_EM);
#endif
#endif

	u.s.hi = JSDOUBLE_HI32_EXPMASK | JSDOUBLE_HI32_MANTMASK;
	u.s.lo = 0xffffffff;
	number_constants[NC_NaN].dval = NaN = u.d;
	rt->jsNaN = js_NewDouble(cx, NaN);
	if (!rt->jsNaN || !js_LockGCThing(cx, rt->jsNaN))
	    return NULL;

	u.s.hi = JSDOUBLE_HI32_EXPMASK;
	u.s.lo = 0x00000000;
	number_constants[NC_POSITIVE_INFINITY].dval = u.d;
	rt->jsPositiveInfinity = js_NewDouble(cx, u.d);
	if (!rt->jsPositiveInfinity ||
	    !js_LockGCThing(cx, rt->jsPositiveInfinity)) {
	    return NULL;
	}

	u.s.hi = JSDOUBLE_HI32_SIGNBIT | JSDOUBLE_HI32_EXPMASK;
	u.s.lo = 0x00000000;
	number_constants[NC_NEGATIVE_INFINITY].dval = u.d;
	rt->jsNegativeInfinity = js_NewDouble(cx, u.d);
	if (!rt->jsNegativeInfinity ||
	    !js_LockGCThing(cx, rt->jsNegativeInfinity)) {
	    return NULL;
	}

	u.s.hi = 0;
	u.s.lo = 1;
	number_constants[NC_MIN_VALUE].dval = u.d;
    }

    if (!JS_DefineFunctions(cx, obj, number_functions))
	return NULL;

    proto = JS_InitClass(cx, obj, NULL, &number_class, Number, 1,
			 NULL, number_methods, NULL, NULL);
    if (!proto || !(ctor = JS_GetConstructor(cx, proto)))
	return NULL;
    OBJ_SET_SLOT(cx, proto, JSSLOT_PRIVATE, JSVAL_ZERO);
    if (!JS_DefineConstDoubles(cx, ctor, number_constants))
	return NULL;

    /* ECMA 15.1.1.1 */
    if (!JS_DefineProperty(cx, obj, "NaN", DOUBLE_TO_JSVAL(rt->jsNaN),
			   NULL, NULL, 0)) {
	return NULL;
    }

    /* ECMA 15.1.1.2 */
    if (!JS_DefineProperty(cx, obj, "Infinity",
			   DOUBLE_TO_JSVAL(rt->jsPositiveInfinity),
			   NULL, NULL, 0)) {
	return NULL;
    }
    return proto;
}

jsdouble *
js_NewDouble(JSContext *cx, jsdouble d)
{
    jsdouble *dp;

    dp = js_AllocGCThing(cx, GCX_DOUBLE);
    if (!dp)
	return NULL;
    *dp = d;
    return dp;
}

void
js_FinalizeDouble(JSContext *cx, jsdouble *dp)
{
    *dp = NaN;
}

JSBool
js_NewDoubleValue(JSContext *cx, jsdouble d, jsval *rval)
{
    jsdouble *dp;

    dp = js_NewDouble(cx, d);
    if (!dp)
	return JS_FALSE;
    *rval = DOUBLE_TO_JSVAL(dp);
    return JS_TRUE;
}

JSBool
js_NewNumberValue(JSContext *cx, jsdouble d, jsval *rval)
{
    jsint i;

    if (JSDOUBLE_IS_INT(d, i) && INT_FITS_IN_JSVAL(i)) {
	*rval = INT_TO_JSVAL(i);
    } else {
	if (!js_NewDoubleValue(cx, d, rval))
	    return JS_FALSE;
    }
    return JS_TRUE;
}

JSObject *
js_NumberToObject(JSContext *cx, jsdouble d)
{
    JSObject *obj;
    jsval v;

    obj = js_NewObject(cx, &number_class, NULL, NULL);
    if (!obj)
	return NULL;
    if (!js_NewNumberValue(cx, d, &v)) {
	cx->newborn[GCX_OBJECT] = NULL;
	return NULL;
    }
    OBJ_SET_SLOT(cx, obj, JSSLOT_PRIVATE, v);
    return obj;
}

/* XXXbe rewrite me to be ECMA-based! */
JSString *
js_NumberToString(JSContext *cx, jsdouble d)
{
    jsint i;
    char buf[32];

    if (JSDOUBLE_IS_INT(d, i)) {
	JS_snprintf(buf, sizeof buf, "%ld", (long)i);
    } else {
	JS_cnvtf(buf, sizeof buf, 20, d);
    }
    return JS_NewStringCopyZ(cx, buf);
}

JSBool
js_ValueToNumber(JSContext *cx, jsval v, jsdouble *dp)
{
    JSObject *obj;
    JSString *str;
    const jschar *ep;
    jsdouble d;

    if (JSVAL_IS_OBJECT(v)) {
	obj = JSVAL_TO_OBJECT(v);
	if (!obj) {
	    *dp = 0;
	    return JS_TRUE;
	}
	if (!OBJ_DEFAULT_VALUE(cx, obj, JSTYPE_NUMBER, &v))
	    return JS_FALSE;
    }
    if (JSVAL_IS_INT(v)) {
	*dp = (jsdouble)JSVAL_TO_INT(v);
    } else if (JSVAL_IS_DOUBLE(v)) {
	*dp = *JSVAL_TO_DOUBLE(v);
    } else if (JSVAL_IS_STRING(v)) {
	str = JSVAL_TO_STRING(v);
	errno = 0;
	/* Note that ECMAScript doesn't treat numbers beginning with a zero as octal numbers here.
	 * This works because all such numbers will be interpreted as decimal by js_strtod and
	 * will never get passed to js_strtointeger, which would interpret them as octal. */
	if ((!js_strtod(cx, str->chars, &ep, &d) || js_SkipWhiteSpace(ep) != str->chars + str->length) &&
	    (!js_strtointeger(cx, str->chars, &ep, 0, &d) || js_SkipWhiteSpace(ep) != str->chars + str->length)) {
	    goto badstr;
	}
	*dp = d;
    } else if (JSVAL_IS_BOOLEAN(v)) {
	*dp = JSVAL_TO_BOOLEAN(v) ? 1 : 0;
    } else {
#if JS_BUG_FALLIBLE_TONUM
	str = js_DecompileValueGenerator(cx, v, NULL);
badstr:
	if (str) {
	    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL, JSMSG_NAN,
				 JS_GetStringBytes(str));

	}
	return JS_FALSE;
#else
badstr:
	*dp = *cx->runtime->jsNaN;
#endif
    }
    return JS_TRUE;
}

JSBool
js_ValueToECMAInt32(JSContext *cx, jsval v, int32 *ip)
{
    jsdouble d;

    if (!js_ValueToNumber(cx, v, &d))
	return JS_FALSE;
    return js_DoubleToECMAInt32(cx, d, ip);
}

JSBool
js_DoubleToECMAInt32(JSContext *cx, jsdouble d, int32 *ip)
{
    jsdouble two32 = 4294967296.0;
    jsdouble two31 = 2147483648.0;

    if (!JSDOUBLE_IS_FINITE(d) || d == 0) {
	*ip = 0;
	return JS_TRUE;
    }
    d = fmod(d, two32);
    d = d >= 0 ? d : d + two32;
    if (d >= two31)
	*ip = (int32)(d - two32);
    else
	*ip = (int32)d;
    return JS_TRUE;
}

JSBool
js_ValueToECMAUint32(JSContext *cx, jsval v, uint32 *ip)
{
    jsdouble d;

    if (!js_ValueToNumber(cx, v, &d))
	return JS_FALSE;
    return js_DoubleToECMAUint32(cx, d, ip);
}

JSBool
js_DoubleToECMAUint32(JSContext *cx, jsdouble d, uint32 *ip)
{
    JSBool neg;
    jsdouble two32 = 4294967296.0;

    if (!JSDOUBLE_IS_FINITE(d) || d == 0) {
	*ip = 0;
	return JS_TRUE;
    }

    neg = (d < 0);
    d = floor(neg ? -d : d);
    d = neg ? -d : d;

    d = fmod(d, two32);

    d = d >= 0 ? d : d + two32;
    *ip = (uint32)d;
    return JS_TRUE;
}

JSBool
js_ValueToInt32(JSContext *cx, jsval v, int32 *ip)
{
    jsdouble d;
    JSString *str;

    if (!js_ValueToNumber(cx, v, &d))
	return JS_FALSE;
    if (JSDOUBLE_IS_NaN(d) || d <= -2147483649.0 || 2147483648.0 <= d) {
	str = js_DecompileValueGenerator(cx, v, NULL);
	if (str) {
	    JS_ReportErrorNumber(cx, js_GetErrorMessage, NULL,
				 JSMSG_CANT_CONVERT, JS_GetStringBytes(str));

	}
	return JS_FALSE;
    }
    *ip = (int32)floor(d + 0.5);     /* Round to nearest */
    return JS_TRUE;
}

JSBool
js_ValueToUint16(JSContext *cx, jsval v, uint16 *ip)
{
    jsdouble d;
    jsuint i, m;
    JSBool neg;

    if (!js_ValueToNumber(cx, v, &d))
	return JS_FALSE;
    if (d == 0 || !JSDOUBLE_IS_FINITE(d)) {
	*ip = 0;
	return JS_TRUE;
    }
    i = (jsuint)d;
    if ((jsdouble)i == d) {
	*ip = (uint16)i;
	return JS_TRUE;
    }
    neg = (d < 0);
    d = floor(neg ? -d : d);
    d = neg ? -d : d;
    m = JS_BIT(16);
    d = fmod(d, m);
    if (d < 0)
	d += m;
    *ip = (uint16) d;
    return JS_TRUE;
}

jsdouble
js_DoubleToInteger(jsdouble d)
{
    JSBool neg;

    if (d == 0)
	return d;
    if (!JSDOUBLE_IS_FINITE(d)) {
	if (JSDOUBLE_IS_NaN(d))
	    return 0;
	return d;
    }
    neg = (d < 0);
    d = floor(neg ? -d : d);
    return neg ? -d : d;
}


JSBool
js_strtod(JSContext *cx, const jschar *s, const jschar **ep, jsdouble *dp)
{
    size_t i;
    char *cstr, *istr, *estr;
    JSBool negative;
    jsdouble d;
    const jschar *s1 = js_SkipWhiteSpace(s);
    size_t length = js_strlen(s1);

    cstr = malloc(length + 1);
    if (!cstr)
	return JS_FALSE;
    for (i = 0; i <= length; i++) {
	if (s1[i] >> 8) {
	    cstr[i] = 0;
	    break;
	}
	cstr[i] = (char)s1[i];
    }

    istr = cstr;
    if ((negative = (*istr == '-')) != 0 || *istr == '+')
	istr++;
    if (!strncmp(istr, "Infinity", 8)) {
	d = *(negative ? cx->runtime->jsNegativeInfinity : cx->runtime->jsPositiveInfinity);
	estr = istr + 8;
    } else {
	errno = 0;
	d = JS_strtod(cstr, &estr);
	if (errno == ERANGE)
	    if (d == HUGE_VAL)
		d = *cx->runtime->jsPositiveInfinity;
	    else if (d == -HUGE_VAL)
		d = *cx->runtime->jsNegativeInfinity;
#ifdef HPUX
        if (d == 0.0 && negative) {
            /* 
             * "-0", "-1e-2000" come out as positive zero
    		 * here on HPUX. Force a negative zero instead.
             */
            JSDOUBLE_HI32(d) = JSDOUBLE_HI32_SIGNBIT;
            JSDOUBLE_LO32(d) = 0;
        }
#endif
    }

    free(cstr);
    i = estr - cstr;
    *ep = i ? s1 + i : s;
    *dp = d;
    return JS_TRUE;
}

struct BinaryDigitReader
{
    uintN base;			/* Base of number; must be a power of 2 */
    uintN digit;		/* Current digit value in radix given by base */
    uintN digitMask;		/* Mask to extract the next bit from digit */
    const jschar *digits;	/* Pointer to the remaining digits */
    const jschar *end;		/* Pointer to first non-digit */
};

/* Return the next binary digit from the number or -1 if done */
static intN GetNextBinaryDigit(struct BinaryDigitReader *bdr)
{
    intN bit;

    if (bdr->digitMask == 0) {
	uintN c;

	if (bdr->digits == bdr->end)
	    return -1;

	c = *bdr->digits++;
	if ('0' <= c && c <= '9')
	    bdr->digit = c - '0';
	else if ('a' <= c && c <= 'z')
	    bdr->digit = c - 'a' + 10;
	else bdr->digit = c - 'A' + 10;
	bdr->digitMask = bdr->base >> 1;
    }
    bit = (bdr->digit & bdr->digitMask) != 0;
    bdr->digitMask >>= 1;
    return bit;
}

JSBool
js_strtointeger(JSContext *cx, const jschar *s, const jschar **ep, jsint base, jsdouble *dp)
{
    JSBool negative;
    jsdouble value;
    const jschar *start;
    const jschar *s1 = js_SkipWhiteSpace(s);

    if ((negative = (*s1 == '-')) != 0 || *s1 == '+')
	s1++;

    if (base == 0)
	/* No base supplied, or some base that evaluated to 0. */
	if (*s1 == '0')
	    /* It's either hex or octal; only increment char if str isn't '0' */
	    if (s1[1] == 'X' || s1[1] == 'x') { /* Hex */
		s1 += 2;
		base = 16;
	    } else /* Octal */
		base = 8;
	else
	    base = 10; /* Default to decimal. */
    else if (base == 16 && *s1 == '0' && (s1[1] == 'X' || s1[1] == 'x'))
	/* If base is 16, ignore hex prefix. */
	s1 += 2;

    /* Done with the preliminaries; find some prefix of the string that's
     * a number in the given base.
     */
    start = s1; /* Mark - if string is empty, we return NaN. */
    value = 0.0;
    while (1) {
	uintN digit;
	jschar c = *s1;
	if ('0' <= c && c <= '9')
	    digit = c - '0';
	else if ('a' <= c && c <= 'z')
	    digit = c - 'a' + 10;
	else if ('A' <= c && c <= 'Z')
	    digit = c - 'A' + 10;
	else
	    break;
	if (digit >= (uintN)base)
	    break;
	value = value*base + digit;
	s1++;
    }

    if (value >= 9007199254740992.0)
	if (base == 10) {
	    /* If we're accumulating a decimal number and the number is >= 2^53, then
	     * the result from the repeated multiply-add above may be inaccurate.  Call
	     * JS_strtod to get the correct answer.
	     */
	    size_t i;
	    size_t length = s1 - start;
	    char *cstr = malloc(length + 1);
	    char *estr;

	    if (!cstr)
		return JS_FALSE;
	    for (i = 0; i != length; i++)
		cstr[i] = (char)start[i];
	    cstr[length] = 0;

	    errno = 0;
	    value = JS_strtod(cstr, &estr);
	    if (errno == ERANGE && value == HUGE_VAL)
		value = *cx->runtime->jsPositiveInfinity;
	    free(cstr);

	} else if (base == 2 || base == 4 || base == 8 || base == 16 || base == 32) {
	    /* The number may also be inaccurate for one of these bases.  This
	     * happens if the addition in value*base + digit causes a round-down
	     * to an even least significant mantissa bit when the first dropped bit
	     * is a one.  If any of the following digits in the number (which haven't
	     * been added in yet) are nonzero then the correct action would have
	     * been to round up instead of down.  An example of this occurs when
	     * reading the number 0x1000000000000081, which rounds to 0x1000000000000000
	     * instead of 0x1000000000000100.
	     */
	    struct BinaryDigitReader bdr;
	    intN bit, bit2;
	    intN j;

	    bdr.base = base;
	    bdr.digitMask = 0;
	    bdr.digits = start;
	    bdr.end = s1;
	    value = 0.0;

	    /* Skip leading zeros. */
	    do {
		bit = GetNextBinaryDigit(&bdr);
	    } while (bit == 0);

	    if (bit == 1) {
		/* Gather the 53 significant bits (including the leading 1) */
		value = 1.0;
		for (j = 52; j; j--) {
		    bit = GetNextBinaryDigit(&bdr);
		    if (bit < 0)
			goto done;
		    value = value*2 + bit;
		}
		/* bit2 is the 54th bit (the first dropped from the mantissa) */
		bit2 = GetNextBinaryDigit(&bdr);
		if (bit2 >= 0) {
		    jsdouble factor = 2.0;
		    intN sticky = 0;  /* sticky is 1 if any bit beyond the 54th is 1 */
		    intN bit3;

		    while ((bit3 = GetNextBinaryDigit(&bdr)) >= 0) {
			sticky |= bit3;
			factor *= 2;
		    }
		    value += bit2 & (bit | sticky);
		    value *= factor;
		}
	      done:;
	    }
	}
    /* We don't worry about inaccurate numbers for any other base. */

    if (s1 == start) {
	*dp = 0.0;
	*ep = s;
    } else {
	*dp = negative ? -value : value;
	*ep = s1;
    }
    return JS_TRUE;
}
