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
 * JS symbol tables.
 */
#include "jsstddef.h"
#include <string.h>
#include "jstypes.h"
#include "jsutil.h" /* Added by JSIFY */
#include "jsapi.h"
#include "jsatom.h"
#include "jscntxt.h"
#include "jsinterp.h"
#include "jslock.h"
#include "jsnum.h"
#include "jsscope.h"
#include "jsstr.h"

JS_STATIC_DLL_CALLBACK(JSHashNumber)
js_hash_id(const void *key)
{
    jsval v;
    const JSAtom *atom;

    v = (jsval)key;
    if (JSVAL_IS_INT(v))
	return JSVAL_TO_INT(v);
    atom = key;
    return atom->number;
}

typedef struct JSScopePrivate {
    JSContext *context;
    JSScope *scope;
} JSScopePrivate;

JS_STATIC_DLL_CALLBACK(void *)
js_alloc_scope_space(void *priv, size_t size)
{
    return JS_malloc(((JSScopePrivate *)priv)->context, size);
}

JS_STATIC_DLL_CALLBACK(void)
js_free_scope_space(void *priv, void *item)
{
    JS_free(((JSScopePrivate *)priv)->context, item);
}

JS_STATIC_DLL_CALLBACK(JSHashEntry *)
js_alloc_symbol(void *priv, const void *key)
{
    JSScopePrivate *spriv;
    JSContext *cx;
    JSSymbol *sym;

    spriv = priv;
    JS_ASSERT(JS_IS_SCOPE_LOCKED(spriv->scope));
    cx = spriv->context;
    sym = JS_malloc(cx, sizeof(JSSymbol));
    if (!sym)
	return NULL;
    sym->entry.key = key;
    return &sym->entry;
}

JS_STATIC_DLL_CALLBACK(void)
js_free_symbol(void *priv, JSHashEntry *he, uintN flag)
{
    JSScopePrivate *spriv;
    JSContext *cx;
    JSSymbol *sym, **sp;
    JSScopeProperty *sprop;

    spriv = priv;
    JS_ASSERT(JS_IS_SCOPE_LOCKED(spriv->scope));
    cx = spriv->context;
    sym = (JSSymbol *)he;
    sprop = sym->entry.value;
    if (sprop) {
	sym->entry.value = NULL;
	sprop = js_DropScopeProperty(cx, spriv->scope, sprop);
	if (sprop) {
	    for (sp = &sprop->symbols; *sp; sp = &(*sp)->next) {
		if (*sp == sym) {
		    *sp = sym->next;
		    if (!*sp)
			break;
		}
	    }
	    sym->next = NULL;
	}
    }

    if (flag == HT_FREE_ENTRY)
	JS_free(cx, he);
}

static JSHashAllocOps hash_scope_alloc_ops = {
    js_alloc_scope_space, js_free_scope_space,
    js_alloc_symbol, js_free_symbol
};

/************************************************************************/

JS_STATIC_DLL_CALLBACK(JSSymbol *)
js_hash_scope_lookup(JSContext *cx, JSScope *scope, jsid id, JSHashNumber hash)
{
    JSHashTable *table = scope->data;
    JSHashEntry **hep;
    JSSymbol *sym;

    hep = JS_HashTableRawLookup(table, hash, (const void *)id);
    sym = (JSSymbol *) *hep;
    return sym;
}

#define SCOPE_ADD(PRIV, CLASS_SPECIFIC_CODE)                                  \
    JS_BEGIN_MACRO                                                            \
	if (sym) {                                                            \
	    if (sym->entry.value == sprop)                                    \
		return sym;                                                   \
	    if (sym->entry.value)                                             \
		js_free_symbol(PRIV, &sym->entry, HT_FREE_VALUE);             \
	} else {                                                              \
	    CLASS_SPECIFIC_CODE                                               \
	    sym->scope = scope;                                               \
	    sym->next = NULL;                                                 \
	}                                                                     \
	if (sprop) {                                                          \
	    sym->entry.value = js_HoldScopeProperty(cx, scope, sprop);        \
	    for (sp = &sprop->symbols; *sp; sp = &(*sp)->next)                \
		;                                                             \
	    *sp = sym;                                                        \
	} else {                                                              \
	    sym->entry.value = NULL;                                          \
	}                                                                     \
    JS_END_MACRO

JS_STATIC_DLL_CALLBACK(JSSymbol *)
js_hash_scope_add(JSContext *cx, JSScope *scope, jsid id, JSScopeProperty *sprop)
{
    JSHashTable *table = scope->data;
    const void *key;
    JSHashNumber keyHash;
    JSHashEntry **hep;
    JSSymbol *sym, **sp;
    JSScopePrivate *priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    priv = table->allocPriv;
    priv->context = cx;
    key = (const void *)id;
    keyHash = js_hash_id(key);
    hep = JS_HashTableRawLookup(table, keyHash, key);
    sym = (JSSymbol *) *hep;
    SCOPE_ADD(priv,
	sym = (JSSymbol *) JS_HashTableRawAdd(table, hep, keyHash, key, NULL);
	if (!sym)
	    return NULL;
    );
    return sym;
}

JS_STATIC_DLL_CALLBACK(JSBool)
js_hash_scope_remove(JSContext *cx, JSScope *scope, jsid id)
{
    JSHashTable *table = scope->data;
    JSScopePrivate *priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    priv = table->allocPriv;
    priv->context = cx;
    return JS_HashTableRemove(table, (const void *)id);
}

/* Forward declaration for use by js_hash_scope_clear(). */
extern JS_FRIEND_DATA(JSScopeOps) js_list_scope_ops;

JS_STATIC_DLL_CALLBACK(void)
js_hash_scope_clear(JSContext *cx, JSScope *scope)
{
    JSHashTable *table = scope->data;
    JSScopePrivate *priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    priv = table->allocPriv;
    priv->context = cx;
    JS_HashTableDestroy(table);
    JS_free(cx, priv);
    scope->ops = &js_list_scope_ops;
    scope->data = NULL;
}

JSScopeOps js_hash_scope_ops = {
    js_hash_scope_lookup,
    js_hash_scope_add,
    js_hash_scope_remove,
    js_hash_scope_clear
};

/************************************************************************/

static void
move_sym_to_front(JSSymbol *sym, JSSymbol **nextp, JSSymbol **front)
{
    *nextp = (JSSymbol*)sym->entry.next;
    sym->entry.next = &(*front)->entry;
    *front = sym;
}

JS_STATIC_DLL_CALLBACK(JSSymbol *)
js_list_scope_lookup(JSContext *cx, JSScope *scope, jsid id, JSHashNumber hash)
{
    JSSymbol *sym, **sp;
    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));

    for (sp = (JSSymbol **)&scope->data; (sym = *sp) != 0;
	 sp = (JSSymbol **)&sym->entry.next) {
	if (sym_id(sym) == id) {
	    /* Move sym to the front for shorter searches. */
            move_sym_to_front(sym,sp,(JSSymbol**)&scope->data);
	    return sym;
	}
    }
    return NULL;
}

#define HASH_THRESHOLD	5

JS_STATIC_DLL_CALLBACK(JSSymbol *)
js_list_scope_add(JSContext *cx, JSScope *scope, jsid id, JSScopeProperty *sprop)
{
    JSSymbol *list = scope->data;
    uint32 nsyms;
    JSSymbol *sym, *next, **sp;
    JSHashTable *table;
    JSHashEntry **hep;
    JSScopePrivate priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    nsyms = 0;
    for (sym = list; sym; sym = (JSSymbol *)sym->entry.next) {
	if (sym_id(sym) == id)
	    break;
	nsyms++;
    }

    if (nsyms >= HASH_THRESHOLD) {
	JSScopePrivate *priv = JS_malloc(cx, sizeof(JSScopePrivate));
	if (!priv) return NULL;
	priv->context = cx;
	priv->scope = scope;
	table = JS_NewHashTable(nsyms, js_hash_id,
				JS_CompareValues, JS_CompareValues,
				&hash_scope_alloc_ops, priv);
	if (table) {
	    for (sym = list; sym; sym = next) {
		/* Save next for loop update, before it changes in lookup. */
		next = (JSSymbol *)sym->entry.next;

		/* Now compute missing keyHash fields. */
		sym->entry.keyHash = js_hash_id(sym->entry.key);
		sym->entry.next = NULL;
		hep = JS_HashTableRawLookup(table,
					    sym->entry.keyHash,
					    sym->entry.key);
		*hep = &sym->entry;
	    }
	    table->nentries = nsyms;
	    scope->ops = &js_hash_scope_ops;
	    scope->data = table;
	    return scope->ops->add(cx, scope, id, sprop);
	}
    }

    priv.context = cx;
    priv.scope = scope;
    SCOPE_ADD(&priv,
	sym = (JSSymbol *)js_alloc_symbol(&priv, (const void *)id);
	if (!sym)
	    return NULL;
	/* Don't set keyHash until we know we need it, above. */
	sym->entry.next = scope->data;
	scope->data = sym;
    );
    return sym;
}

JS_STATIC_DLL_CALLBACK(JSBool)
js_list_scope_remove(JSContext *cx, JSScope *scope, jsid id)
{
    JSSymbol *sym, **sp;
    JSScopePrivate priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    for (sp = (JSSymbol **)&scope->data; (sym = *sp) != 0;
	 sp = (JSSymbol **)&sym->entry.next) {
	if (sym_id(sym) == id) {
	    *sp = (JSSymbol *)sym->entry.next;
	    priv.context = cx;
	    priv.scope = scope;
	    js_free_symbol(&priv, &sym->entry, HT_FREE_ENTRY);
	    return JS_TRUE;
	}
    }
    return JS_FALSE;
}

JS_STATIC_DLL_CALLBACK(void)
js_list_scope_clear(JSContext *cx, JSScope *scope)
{
    JSSymbol *sym;
    JSScopePrivate priv;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    while ((sym = scope->data) != NULL) {
	scope->data = sym->entry.next;
	priv.context = cx;
	priv.scope = scope;
	js_free_symbol(&priv, &sym->entry, HT_FREE_ENTRY);
    }
}

JSScopeOps JS_FRIEND_DATA(js_list_scope_ops) = {
    js_list_scope_lookup,
    js_list_scope_add,
    js_list_scope_remove,
    js_list_scope_clear
};

/************************************************************************/

JSScope *
js_GetMutableScope(JSContext *cx, JSObject *obj)
{
    JSScope *scope, *newscope;

    scope = (JSScope *) obj->map;
    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    if (scope->object == obj)
	return scope;
    newscope = js_NewScope(cx, 0, scope->map.ops, LOCKED_OBJ_GET_CLASS(obj),
			   obj);
    if (!newscope)
	return NULL;
    JS_LOCK_SCOPE(cx, newscope);
    obj->map = js_HoldObjectMap(cx, &newscope->map);
    scope = (JSScope *) js_DropObjectMap(cx, &scope->map, obj);
    JS_TRANSFER_SCOPE_LOCK(cx, scope, newscope);
    return newscope;
}

JSScope *
js_MutateScope(JSContext *cx, JSObject *obj, jsid id,
	       JSPropertyOp getter, JSPropertyOp setter, uintN attrs,
	       JSScopeProperty **propp)
{
    /* XXX pessimal */
    *propp = NULL;
    return js_GetMutableScope(cx, obj);
}

JSScope *
js_NewScope(JSContext *cx, jsrefcount nrefs, JSObjectOps *ops, JSClass *clasp,
	    JSObject *obj)
{
    JSScope *scope;

    scope = JS_malloc(cx, sizeof(JSScope));
    if (!scope)
	return NULL;
    js_InitObjectMap(&scope->map, nrefs, ops, clasp);
    scope->object = obj;
    scope->props = NULL;
    scope->proptail = &scope->props;
    scope->ops = &js_list_scope_ops;
    scope->data = NULL;

#ifdef JS_THREADSAFE
    js_NewLock(&scope->lock);
    scope->count = 0;
#ifdef DEBUG
    scope->file[0] = scope->file[1] = scope->file[2] = scope->file[3] = NULL;
    scope->line[0] = scope->line[1] = scope->line[2] = scope->line[3] = 0;
#endif
#endif

    return scope;
}

void
js_DestroyScope(JSContext *cx, JSScope *scope)
{
    JS_LOCK_SCOPE(cx, scope);
    scope->ops->clear(cx, scope);
    JS_UNLOCK_SCOPE(cx, scope);
#ifdef JS_THREADSAFE
    JS_ASSERT(scope->count == 0);
    js_DestroyLock(&scope->lock);
#endif
    JS_free(cx, scope);
}

JSHashNumber
js_HashValue(jsval v)
{
    return js_hash_id((const void *)v);
}

jsval
js_IdToValue(jsid id)
{
    JSAtom *atom;

    if (JSVAL_IS_INT(id))
	return id;
    atom = (JSAtom *)id;
    return ATOM_KEY(atom);
}

JSScopeProperty *
js_NewScopeProperty(JSContext *cx, JSScope *scope, jsid id,
		    JSPropertyOp getter, JSPropertyOp setter, uintN attrs)
{
    uint32 slot;
    JSScopeProperty *sprop;

    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    if (!js_AllocSlot(cx, scope->object, &slot))
	return NULL;
    sprop = JS_malloc(cx, sizeof(JSScopeProperty));
    if (!sprop) {
	js_FreeSlot(cx, scope->object, slot);
	return NULL;
    }
    sprop->nrefs = 0;
    sprop->id = js_IdToValue(id);
    sprop->getter = getter;
    sprop->setter = setter;
    sprop->slot = slot;
    sprop->attrs = attrs;
    sprop->spare = 0;
    sprop->symbols = NULL;
    sprop->next = NULL;
    sprop->prevp = scope->proptail;
    *scope->proptail = sprop;
    scope->proptail = &sprop->next;
    return sprop;
}

void
js_DestroyScopeProperty(JSContext *cx, JSScope *scope, JSScopeProperty *sprop)
{
    /*
     * Test whether obj was finalized before prop's last dereference.
     *
     * This can happen when a deleted property is held by a property iterator
     * object (which points to obj, keeping obj alive so long as the property
     * iterator is reachable).  As soon as the GC finds the iterator to be
     * unreachable, it will finalize the iterator, and may also finalize obj,
     * in an unpredictable order.  If obj is finalized first, its map will be
     * null below, and we need only free prop.
     *
     * In the more typical case of a property whose last reference (from a
     * symbol in obj's scope) goes away before obj is finalized, we must be
     * sure to free prop's slot and unlink it from obj's property list.
     */
    if (scope) {
	JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
	if (scope->object) {
	    js_FreeSlot(cx, scope->object, sprop->slot);
	    *sprop->prevp = sprop->next;
	    if (sprop->next)
		sprop->next->prevp = sprop->prevp;
	    else
		scope->proptail = sprop->prevp;
	}
    }

    /* Purge any cached weak links to prop, then free it. */
    js_FlushPropertyCacheByProp(cx, (JSProperty *)sprop);
    JS_free(cx, sprop);
}

JSScopeProperty *
js_HoldScopeProperty(JSContext *cx, JSScope *scope, JSScopeProperty *sprop)
{
    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    if (sprop) {
	JS_ASSERT(sprop->nrefs >= 0);
	sprop->nrefs++;
    }
    return sprop;
}

JSScopeProperty *
js_DropScopeProperty(JSContext *cx, JSScope *scope, JSScopeProperty *sprop)
{
    JS_ASSERT(JS_IS_SCOPE_LOCKED(scope));
    if (sprop) {
	JS_ASSERT(sprop->nrefs > 0);
	if (--sprop->nrefs == 0) {
	    js_DestroyScopeProperty(cx, scope, sprop);
	    sprop = NULL;
	}
    }
    return sprop;
}
