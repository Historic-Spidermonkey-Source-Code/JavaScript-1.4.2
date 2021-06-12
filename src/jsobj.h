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

#ifndef jsobj_h___
#define jsobj_h___
/*
 * JS object definitions.
 *
 * A JS object consists of a possibly-shared object descriptor containing
 * ordered property names, called the map; and a dense vector of property
 * values, called slots.  The map/slot pointer pair is GC'ed, while the map
 * is reference counted and the slot vector is malloc'ed.
 */
#include "jshash.h" /* Added by JSIFY */
#include "jsprvtd.h"
#include "jspubtd.h"

JS_BEGIN_EXTERN_C

struct JSObjectMap {
    jsrefcount  nrefs;          /* count of all referencing objects */
    JSObjectOps *ops;           /* high level object operation vtable */
    uint32      nslots;         /* length of obj->slots vector */
    uint32      freeslot;       /* index of next free obj->slots element */
};

/* Shorthand macros for frequently-made calls. */
#if defined JS_THREADSAFE && defined DEBUG
#define OBJ_LOOKUP_PROPERTY(cx,obj,id,objp,propp)                             \
    (obj)->map->ops->lookupProperty(cx,obj,id,objp,propp,__FILE__,__LINE__)
#else
#define OBJ_LOOKUP_PROPERTY(cx,obj,id,objp,propp)                             \
    (obj)->map->ops->lookupProperty(cx,obj,id,objp,propp)
#endif
#define OBJ_DEFINE_PROPERTY(cx,obj,id,value,getter,setter,attrs,propp)        \
    (obj)->map->ops->defineProperty(cx,obj,id,value,getter,setter,attrs,propp)
#define OBJ_GET_PROPERTY(cx,obj,id,vp)                                        \
    (obj)->map->ops->getProperty(cx,obj,id,vp)
#define OBJ_SET_PROPERTY(cx,obj,id,vp)                                        \
    (obj)->map->ops->setProperty(cx,obj,id,vp)
#define OBJ_GET_ATTRIBUTES(cx,obj,id,prop,attrsp)                             \
    (obj)->map->ops->getAttributes(cx,obj,id,prop,attrsp)
#define OBJ_SET_ATTRIBUTES(cx,obj,id,prop,attrsp)                             \
    (obj)->map->ops->setAttributes(cx,obj,id,prop,attrsp)
#define OBJ_DELETE_PROPERTY(cx,obj,id,rval)                                   \
    (obj)->map->ops->deleteProperty(cx,obj,id,rval)
#define OBJ_DEFAULT_VALUE(cx,obj,hint,vp)                                     \
    (obj)->map->ops->defaultValue(cx,obj,hint,vp)
#define OBJ_ENUMERATE(cx,obj,enum_op,statep,idp)                                                 \
    (obj)->map->ops->enumerate(cx,obj,enum_op,statep,idp)
#define OBJ_CHECK_ACCESS(cx,obj,id,mode,vp,attrsp)                            \
    (obj)->map->ops->checkAccess(cx,obj,id,mode,vp,attrsp)

/* These two are time-optimized to avoid stub calls. */
#define OBJ_THIS_OBJECT(cx,obj)                                               \
    ((obj)->map->ops->thisObject                                              \
     ? (obj)->map->ops->thisObject(cx,obj)                                    \
     : (obj))
#define OBJ_DROP_PROPERTY(cx,obj,prop)                                        \
    ((obj)->map->ops->dropProperty                                            \
     ? (obj)->map->ops->dropProperty(cx,obj,prop)                             \
     : (void)0)

struct JSObject {
    JSObjectMap *map;
    jsval       *slots;
};

#define JSSLOT_PROTO        0
#define JSSLOT_PARENT       1
#define JSSLOT_CLASS        2
#define JSSLOT_PRIVATE      3
#define JSSLOT_START        3

#define JSSLOT_FREE(clasp)  (((clasp)->flags & JSCLASS_HAS_PRIVATE)           \
			     ? JSSLOT_PRIVATE + 1                             \
			     : JSSLOT_START)

#define JS_INITIAL_NSLOTS   5

#ifdef DEBUG
#define MAP_CHECK_SLOT(map,slot) \
    JS_ASSERT((uint32)slot < JS_MAX((map)->nslots, (map)->freeslot))
#define OBJ_CHECK_SLOT(obj,slot) \
    MAP_CHECK_SLOT((obj)->map, slot)
#else
#define OBJ_CHECK_SLOT(obj,slot) ((void)0)
#endif

/* Fast macros for accessing obj->slots while obj is locked (if thread-safe). */
#define LOCKED_OBJ_GET_SLOT(obj,slot) \
    (OBJ_CHECK_SLOT(obj, slot), (obj)->slots[slot])
#define LOCKED_OBJ_SET_SLOT(obj,slot,value) \
    (OBJ_CHECK_SLOT(obj, slot), (obj)->slots[slot] = (value))
#define LOCKED_OBJ_GET_PROTO(obj) \
    JSVAL_TO_OBJECT(LOCKED_OBJ_GET_SLOT(obj, JSSLOT_PROTO))
#define LOCKED_OBJ_GET_CLASS(obj) \
    ((JSClass *)JSVAL_TO_PRIVATE(LOCKED_OBJ_GET_SLOT(obj, JSSLOT_CLASS)))

#ifdef JS_THREADSAFE

/* Thread-safe functions and wrapper macros for accessing obj->slots. */
#define OBJ_GET_SLOT(cx,obj,slot) \
    (OBJ_CHECK_SLOT(obj, slot), js_GetSlotWhileLocked(cx, obj, slot))
#define OBJ_SET_SLOT(cx,obj,slot,value) \
    (OBJ_CHECK_SLOT(obj, slot), js_SetSlotWhileLocked(cx, obj, slot, value))

#else   /* !JS_THREADSAFE */

#define OBJ_GET_SLOT(cx,obj,slot)       LOCKED_OBJ_GET_SLOT(obj,slot)
#define OBJ_SET_SLOT(cx,obj,slot,value) LOCKED_OBJ_SET_SLOT(obj,slot,value)

#endif /* !JS_THREADSAFE */

/* Thread-safe proto, parent, and class access macros. */
#define OBJ_GET_PROTO(cx,obj) \
    JSVAL_TO_OBJECT(OBJ_GET_SLOT(cx, obj, JSSLOT_PROTO))
#define OBJ_SET_PROTO(cx,obj,proto) \
    OBJ_SET_SLOT(cx, obj, JSSLOT_PROTO, OBJECT_TO_JSVAL(proto))

#define OBJ_GET_PARENT(cx,obj) \
    JSVAL_TO_OBJECT(OBJ_GET_SLOT(cx, obj, JSSLOT_PARENT))
#define OBJ_SET_PARENT(cx,obj,parent) \
    OBJ_SET_SLOT(cx, obj, JSSLOT_PARENT, OBJECT_TO_JSVAL(parent))

#define OBJ_GET_CLASS(cx,obj) \
    ((JSClass *)JSVAL_TO_PRIVATE(OBJ_GET_SLOT(cx, obj, JSSLOT_CLASS)))

/* Test whether a map or object is native. */
#define MAP_IS_NATIVE(map)  ((map)->ops == &js_ObjectOps || \
                             (map)->ops == &js_WithObjectOps)
#define OBJ_IS_NATIVE(obj)  MAP_IS_NATIVE((obj)->map)

extern JS_FRIEND_DATA(JSObjectOps) js_ObjectOps;
extern JS_FRIEND_DATA(JSObjectOps) js_WithObjectOps;
extern JSClass      js_ObjectClass;
extern JSClass      js_WithClass;

struct JSSharpObjectMap {
    jsrefcount  depth;
    jsatomid    sharpgen;
    JSHashTable *table;
};

#define SHARP_BIT       1
#define IS_SHARP(he)	((jsatomid)(he)->value & SHARP_BIT)
#define MAKE_SHARP(he)  ((he)->value = (void*)((jsatomid)(he)->value|SHARP_BIT))

extern JSHashEntry *
js_EnterSharpObject(JSContext *cx, JSObject *obj, JSIdArray **idap,
		    jschar **sp);

extern void
js_LeaveSharpObject(JSContext *cx, JSIdArray **idap);

extern JSBool
js_obj_toSource(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
		jsval *rval);

extern JSBool
js_obj_toString(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
		jsval *rval);

extern JSObject *
js_InitObjectClass(JSContext *cx, JSObject *obj);

extern void
js_InitObjectMap(JSObjectMap *map, jsrefcount nrefs, JSObjectOps *ops,
		 JSClass *clasp);

extern JSObjectMap *
js_NewObjectMap(JSContext *cx, jsrefcount nrefs, JSObjectOps *ops,
		JSClass *clasp, JSObject *obj);

extern void
js_DestroyObjectMap(JSContext *cx, JSObjectMap *map);

extern JSObjectMap *
js_HoldObjectMap(JSContext *cx, JSObjectMap *map);

extern JSObjectMap *
js_DropObjectMap(JSContext *cx, JSObjectMap *map, JSObject *obj);

extern JSObject *
js_NewObject(JSContext *cx, JSClass *clasp, JSObject *proto, JSObject *parent);

extern JSObject *
js_ConstructObject(JSContext *cx, JSClass *clasp, JSObject *proto,
		   JSObject *parent);

extern void
js_FinalizeObject(JSContext *cx, JSObject *obj);

extern JSBool
js_AllocSlot(JSContext *cx, JSObject *obj, uint32 *slotp);

extern void
js_FreeSlot(JSContext *cx, JSObject *obj, uint32 slot);

/*
 * On error, return false.  On success, if propp is non-null, return true with
 * obj locked and with a held property in *propp; if propp is null, return true
 * but release obj's lock first.  Therefore all callers who pass non-null propp
 * result parameters must later call OBJ_DROP_PROPERTY(cx, obj, *propp) both to
 * drop the held property, and to release the lock on obj.
 */
extern JSBool
js_DefineProperty(JSContext *cx, JSObject *obj, jsid id, jsval value,
		  JSPropertyOp getter, JSPropertyOp setter, uintN attrs,
		  JSProperty **propp);

/*
 * Unlike js_DefineProperty, propp must be non-null.  On success, and if id was
 * found, return true with *objp non-null and locked, and with a held property
 * stored in *propp.  If successful but id was not found, return true with both
 * *objp and *propp null.  Therefore all callers who receive a non-null *propp
 * must later call OBJ_DROP_PROPERTY(cx, *objp, *propp).
 */
#if defined JS_THREADSAFE && defined DEBUG
extern JS_FRIEND_API(JSBool)
_js_LookupProperty(JSContext *cx, JSObject *obj, jsid id, JSObject **objp,
		   JSProperty **propp, const char *file, uintN line);

#define js_LookupProperty(cx,obj,id,objp,propp) \
    _js_LookupProperty(cx,obj,id,objp,propp,__FILE__,__LINE__)
#else
extern JS_FRIEND_API(JSBool)
js_LookupProperty(JSContext *cx, JSObject *obj, jsid id, JSObject **objp,
		  JSProperty **propp);
#endif

extern JS_FRIEND_API(JSBool)
js_FindProperty(JSContext *cx, jsid id, JSObject **objp, JSObject **pobjp,
		JSProperty **propp);

extern JSBool
js_FindVariable(JSContext *cx, jsid id, JSObject **objp, JSObject **pobjp,
		JSProperty **propp);

extern JSObject *
js_FindVariableScope(JSContext *cx, JSFunction **funp);

extern JSBool
js_GetProperty(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JSBool
js_SetProperty(JSContext *cx, JSObject *obj, jsid id, jsval *vp);

extern JSBool
js_GetAttributes(JSContext *cx, JSObject *obj, jsid id, JSProperty *prop,
		 uintN *attrsp);

extern JSBool
js_SetAttributes(JSContext *cx, JSObject *obj, jsid id, JSProperty *prop,
		 uintN *attrsp);

extern JSBool
js_DeleteProperty(JSContext *cx, JSObject *obj, jsid id, jsval *rval);

extern JSBool
js_DefaultValue(JSContext *cx, JSObject *obj, JSType hint, jsval *vp);

extern JSBool
js_Enumerate(JSContext *cx, JSObject *obj, JSIterateOp enum_op,
	     jsval *statep, jsid *idp);

extern JSBool
js_CheckAccess(JSContext *cx, JSObject *obj, jsid id, JSAccessMode mode,
	       jsval *vp, uintN *attrsp);

extern JSBool
js_Call(JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval);

extern JSBool
js_Construct(JSContext *cx, JSObject *obj, uintN argc, jsval *argv,
	     jsval *rval);

extern JSBool
js_HasInstance(JSContext *cx, JSObject *obj, jsval v, JSBool *bp);

extern JSBool
js_IsDelegate(JSContext *cx, JSObject *obj, jsval v, JSBool *bp);

extern JSBool
js_GetClassPrototype(JSContext *cx, const char *name, JSObject **protop);

extern JSBool
js_SetClassPrototype(JSContext *cx, JSObject *ctor, JSObject *proto,
		     uintN attrs);

extern JSBool
js_ValueToObject(JSContext *cx, jsval v, JSObject **objp);

extern JSObject *
js_ValueToNonNullObject(JSContext *cx, jsval v);

extern JSBool
js_TryValueOf(JSContext *cx, JSObject *obj, JSType type, jsval *rval);

extern JSBool
js_TryMethod(JSContext *cx, JSObject *obj, JSAtom *atom,
	     uintN argc, jsval *argv, jsval *rval);

extern JSBool
js_XDRObject(JSXDRState *xdr, JSObject **objp);

extern JSIdArray *
js_NewIdArray(JSContext *cx, jsint length);

JS_END_EXTERN_C

#endif /* jsobj_h___ */
