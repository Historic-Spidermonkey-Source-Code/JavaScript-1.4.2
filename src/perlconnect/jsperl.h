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

/* This is the public header, which means that this is the only thing one    */
/* needs to include to enable the Perl object. See README.html for more      */
/* documentation                                                             */

#include "jsapi.h"

/*
    This is the only function that must be called by an
    application that wants to use PerlConnect.
*/
extern PR_PUBLIC_API(JSObject*)
JS_InitPerlClass(JSContext *cx, JSObject *obj);
