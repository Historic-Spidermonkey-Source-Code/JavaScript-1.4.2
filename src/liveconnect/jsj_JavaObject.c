/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License.  You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.  See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is Netscape Communications
 * Corporation.  Portions created by Netscape are Copyright (C) 1998
 * Netscape Communications Corporation.  All Rights Reserved.
 */

/*
 * This file is part of the Java-vendor-neutral implementation of LiveConnect
 *
 * It contains the native code implementation of JS's JavaObject class.
 *
 * An instance of JavaObject is the JavaScript reflection of a Java object.
 *
 */

#include <stdlib.h>
#include <string.h>

#include "jsj_private.h"      /* LiveConnect internals */
#include "jsj_hash.h"         /* Hash table with Java object as key */

#ifdef JSJ_THREADSAFE
#include "prmon.h"
#endif

/*
 * This is a hash table that maps from Java objects to JS objects.
 * It is used to ensure that the same JS object is obtained when a Java
 * object is reflected more than once, so that JS object equality tests
 * work in the expected manner, i.e. the "==" and "===" operators.
 *
 * The table entry keys are Java objects (of type jobject) and the entry values
 * are JSObject pointers.  Because the jobject type is an opaque handle and
 * not necessarily a pointer, the hashing and key comparison functions must
 * invoke the appropriate JVM functions.
 *
 * When the corresponding JS object instance is finalized, the entry is
 * removed from the table, and a Java GC root for the Java object is removed.
 */
static JSJHashTable *java_obj_reflections = NULL;

#ifdef JSJ_THREADSAFE
static PRMonitor *java_obj_reflections_monitor = NULL;
#endif

JSBool
jsj_InitJavaObjReflectionsTable(void)
{
    JS_ASSERT(!java_obj_reflections);

    java_obj_reflections =
        JSJ_NewHashTable(512, jsj_HashJavaObject, jsj_JavaObjectComparator,
                         NULL, NULL, NULL);
    if (!java_obj_reflections)
        return JS_FALSE;

#ifdef JSJ_THREADSAFE
    java_obj_reflections_monitor = 
	(struct PRMonitor *) PR_NewNamedMonitor("java_obj_reflections");
    if (!java_obj_reflections_monitor) {
        JS_HashTableDestroy(java_obj_reflections);
        return JS_FALSE;
    }
#endif

    return JS_TRUE;
}

JSObject *
jsj_WrapJavaObject(JSContext *cx,
                   JNIEnv *jEnv,
                   jobject java_obj,
                   jclass java_class)
{
    JSJHashNumber hash_code;
    JSClass *js_class;
    JSObject *js_wrapper_obj;
    JavaObjectWrapper *java_wrapper;
    JavaClassDescriptor *class_descriptor;
    JSJHashEntry *he, **hep;

    js_wrapper_obj = NULL;

    hash_code = jsj_HashJavaObject((void*)java_obj, (void*)jEnv);

#ifdef JSJ_THREADSAFE
    PR_EnterMonitor(java_obj_reflections_monitor);
#endif
    
    hep = JSJ_HashTableRawLookup(java_obj_reflections,
                                 hash_code, java_obj, (void*)jEnv);
    he = *hep;
    if (he) {
        js_wrapper_obj = (JSObject *)he->value;
        if (js_wrapper_obj)
            goto done;
    }

    /* No existing reflection found.  Construct a new one */
    class_descriptor = jsj_GetJavaClassDescriptor(cx, jEnv, java_class);
    if (!class_descriptor)
        goto done;
    if (class_descriptor->type == JAVA_SIGNATURE_ARRAY) {
        js_class = &JavaArray_class;
    } else {
        JS_ASSERT(IS_OBJECT_TYPE(class_descriptor->type));
        js_class = &JavaObject_class;
    }
    
    /* Create new JS object to reflect Java object */
    js_wrapper_obj = JS_NewObject(cx, js_class, NULL, NULL);
    if (!js_wrapper_obj)
        goto done;

    /* Create private, native portion of JavaObject */
    java_wrapper =
        (JavaObjectWrapper *)JS_malloc(cx, sizeof(JavaObjectWrapper));
    if (!java_wrapper) {
        jsj_ReleaseJavaClassDescriptor(cx, jEnv, class_descriptor);
        goto done;
    }
    JS_SetPrivate(cx, js_wrapper_obj, java_wrapper);
    java_wrapper->class_descriptor = class_descriptor;

    java_obj = (*jEnv)->NewGlobalRef(jEnv, java_obj);
    java_wrapper->java_obj = java_obj;
    if (!java_obj)
        goto out_of_memory;

    /* Add the JavaObject to the hash table */
    he = JSJ_HashTableRawAdd(java_obj_reflections, hep, hash_code,
                             java_obj, js_wrapper_obj, (void*)jEnv);
    if (!he) {
        (*jEnv)->DeleteGlobalRef(jEnv, java_obj);
        goto out_of_memory;
    } 

done:
#ifdef JSJ_THREADSAFE
        PR_ExitMonitor(java_obj_reflections_monitor);
#endif
        
    return js_wrapper_obj;

out_of_memory:
    /* No need to free js_wrapper_obj, as it will be finalized by GC. */
    JS_ReportOutOfMemory(cx);
    js_wrapper_obj = NULL;
    goto done;
}

static void
remove_java_obj_reflection_from_hashtable(jobject java_obj, JNIEnv *jEnv)
{
    JSJHashNumber hash_code;
    JSJHashEntry *he, **hep;

    hash_code = jsj_HashJavaObject((void*)java_obj, (void*)jEnv);

#ifdef JSJ_THREADSAFE
    PR_EnterMonitor(java_obj_reflections_monitor);
#endif

    hep = JSJ_HashTableRawLookup(java_obj_reflections, hash_code,
                                 java_obj, (void*)jEnv);
    he = *hep;

    JS_ASSERT(he);
    if (he)
        JSJ_HashTableRawRemove(java_obj_reflections, hep, he, (void*)jEnv);

#ifdef JSJ_THREADSAFE
    PR_ExitMonitor(java_obj_reflections_monitor);
#endif
}

void
JavaObject_finalize(JSContext *cx, JSObject *obj)
{
    JavaObjectWrapper *java_wrapper;
    jobject java_obj;
    JNIEnv *jEnv;

    java_wrapper = JS_GetPrivate(cx, obj);
    if (!java_wrapper)
        return;
    java_obj = java_wrapper->java_obj;

    jsj_MapJSContextToJSJThread(cx, &jEnv);
    if (!jEnv)
        return;

    if (java_obj) {
        remove_java_obj_reflection_from_hashtable(java_obj, jEnv);
        (*jEnv)->DeleteGlobalRef(jEnv, java_obj);
    }
    jsj_ReleaseJavaClassDescriptor(cx, jEnv, java_wrapper->class_descriptor);
    JS_free(cx, java_wrapper);
}

/* Trivial helper for jsj_DiscardJavaObjReflections(), below */
static JSIntn
enumerate_remove_java_obj(JSJHashEntry *he, JSIntn i, void *arg)
{
    JNIEnv *jEnv = (JNIEnv*)arg;
    jobject java_obj;
    JavaObjectWrapper *java_wrapper;
    JSObject *java_wrapper_obj;

    java_wrapper_obj = (JSObject *)he->value;
    java_wrapper = JS_GetPrivate(NULL, java_wrapper_obj);
    java_obj = java_wrapper->java_obj;
    (*jEnv)->DeleteGlobalRef(jEnv, java_obj);
    java_wrapper->java_obj = NULL;
    return HT_ENUMERATE_REMOVE;
}

/* This shutdown routine discards all JNI references to Java objects
   that have been reflected into JS, even if there are still references
   to them from JS. */
void
jsj_DiscardJavaObjReflections(JNIEnv *jEnv)
{
    if (java_obj_reflections) {
        JSJ_HashTableEnumerateEntries(java_obj_reflections,
                                      enumerate_remove_java_obj,
                                      (void*)jEnv);
        JSJ_HashTableDestroy(java_obj_reflections);
        java_obj_reflections = NULL;
    }
}

JS_DLL_CALLBACK JSBool
JavaObject_convert(JSContext *cx, JSObject *obj, JSType type, jsval *vp)
{
    JavaObjectWrapper *java_wrapper;
    JavaClassDescriptor *class_descriptor;
    jobject java_obj;
    JNIEnv *jEnv;
    
    /* Get the Java per-thread environment pointer for this JSContext */
    jsj_MapJSContextToJSJThread(cx, &jEnv);
    if (!jEnv)
        return JS_FALSE;
        
    java_wrapper = JS_GetPrivate(cx, obj);
    if (!java_wrapper) {
        if (type == JSTYPE_OBJECT) {
            *vp = OBJECT_TO_JSVAL(obj);
            return JS_TRUE;
        }
        
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                                JSJMSG_BAD_OP_JOBJECT);
        return JS_FALSE;
    }

    java_obj = java_wrapper->java_obj;
    class_descriptor = java_wrapper->class_descriptor;

    switch (type) {
    case JSTYPE_OBJECT:
        *vp = OBJECT_TO_JSVAL(obj);
        return JS_TRUE;

    case JSTYPE_FUNCTION:
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                                JSJMSG_CONVERT_TO_FUNC);
        return JS_FALSE;

    case JSTYPE_VOID:
    case JSTYPE_STRING:
        /* Either extract a C-string from the java.lang.String object
           or call the Java toString() method */
        return jsj_ConvertJavaObjectToJSString(cx, jEnv, class_descriptor, java_obj, vp);

    case JSTYPE_NUMBER:
        /* Call Java doubleValue() method, if applicable */
        return jsj_ConvertJavaObjectToJSNumber(cx, jEnv, class_descriptor, java_obj, vp);

    case JSTYPE_BOOLEAN:
        /* Call booleanValue() method, if applicable */
        return jsj_ConvertJavaObjectToJSBoolean(cx, jEnv, class_descriptor, java_obj, vp);

    default:
        JS_ASSERT(0);
        return JS_FALSE;
    }
}

/*
 * Get a property from the prototype object of a native ECMA object, i.e.
 * return <js_constructor_name>.prototype.<member_name>
 * This is used to allow Java objects to inherit methods from Array.prototype
 * and String.prototype.
 */
static JSBool
inherit_props_from_JS_natives(JSContext *cx, const char *js_constructor_name,
                              const char *member_name, jsval *vp)
{
    JSObject *global_obj, *constructor_obj, *prototype_obj;
    jsval constructor_val, prototype_val;
    
    global_obj = JS_GetGlobalObject(cx);
    JS_ASSERT(global_obj);
    if (!global_obj)
        return JS_FALSE;

    JS_GetProperty(cx, global_obj, js_constructor_name, &constructor_val);
    JS_ASSERT(JSVAL_IS_OBJECT(constructor_val));
    constructor_obj = JSVAL_TO_OBJECT(constructor_val);

    JS_GetProperty(cx, constructor_obj, "prototype", &prototype_val);
    JS_ASSERT(JSVAL_IS_OBJECT(prototype_val));
    prototype_obj = JSVAL_TO_OBJECT(prototype_val);

    return JS_GetProperty(cx, prototype_obj, member_name, vp) && *vp != JSVAL_VOID;
}

static JSBool
lookup_member_by_id(JSContext *cx, JNIEnv *jEnv, JSObject *obj,
                    JavaObjectWrapper **java_wrapperp,
                    jsid id,
                    JavaMemberDescriptor **member_descriptorp,
                    jsval *vp)
{
    jsval idval;
    JavaObjectWrapper *java_wrapper;
    JavaMemberDescriptor *member_descriptor;
    const char *member_name;
    JavaClassDescriptor *class_descriptor;
    JSObject *proto_chain;
    
    member_descriptor = NULL;
    java_wrapper = JS_GetPrivate(cx, obj);

    /* Handle accesses to prototype object */
    if (!java_wrapper) {
        if (JS_IdToValue(cx, id, &idval) && JSVAL_IS_STRING(idval) &&
            (member_name = JS_GetStringBytes(JSVAL_TO_STRING(idval))) != NULL) {
            if (!strcmp(member_name, "constructor"))
                goto done;
        }
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, JSJMSG_BAD_OP_JOBJECT);
        return JS_FALSE;
    }
    
    class_descriptor = java_wrapper->class_descriptor;
    JS_ASSERT(IS_REFERENCE_TYPE(class_descriptor->type));
    
    member_descriptor = jsj_LookupJavaMemberDescriptorById(cx, jEnv, class_descriptor, id);
    if (member_descriptor)
        goto done;
    
    /* Instances can reference static methods and fields */
    member_descriptor = jsj_LookupJavaStaticMemberDescriptorById(cx, jEnv, class_descriptor, id);
    if (member_descriptor)
        goto done;

    /* Ensure that the property we're searching for is string-valued. */
    JS_IdToValue(cx, id, &idval);
    if (!JSVAL_IS_STRING(idval)) {
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, JSJMSG_BAD_JOBJECT_EXPR);
        return JS_FALSE;
    }
    member_name = JS_GetStringBytes(JSVAL_TO_STRING(idval));

    /*
     * A little LC3 feature magic:
     *   + Instances of java.lang.String "inherit" the standard ECMA string methods
     *     of String.prototype.  All of the ECMA string methods convert the Java
     *     string to a JS string before performing the string operation.  For example,
     *         s = new java.lang.String("foobar");
     *         return s.slice(2);
     *   + Similarly, instances of Java arrays "inherit" the standard ECMA array
     *     methods of Array.prototype.  (Not all of these methods work properly
     *     on JavaArray objects, however, since the 'length' property is read-only.)
     */
    if (vp) {
        if ((class_descriptor->type == JAVA_SIGNATURE_JAVA_LANG_STRING) &&
            inherit_props_from_JS_natives(cx, "String", member_name, vp))
            goto done;
        if ((class_descriptor->type == JAVA_SIGNATURE_ARRAY) &&
            inherit_props_from_JS_natives(cx, "Array", member_name, vp))
            goto done;
    }

    /* Check for access to magic prototype chain property */
    if (!strcmp(member_name, "__proto__")) {
        proto_chain = JS_GetPrototype(cx, obj);
        if (vp)
            *vp = OBJECT_TO_JSVAL(proto_chain);
        goto done;
    }
    
    /*
     * See if the property looks like the explicit resolution of an
     * overloaded method, e.g. "max(double,double)", first as an instance method,
     * then as a static method.  If we find such a method, it will be cached
     * so future accesses won't run this code.
     */
    member_descriptor = jsj_ResolveExplicitMethod(cx, jEnv, class_descriptor, id, JS_FALSE);
    if (member_descriptor)
        goto done;
    member_descriptor = jsj_ResolveExplicitMethod(cx, jEnv, class_descriptor, id, JS_TRUE);
    if (member_descriptor)
        goto done;
    
    /* Is this lookup on behalf of a GetProperty or a LookupProperty ? */
    if (vp) {
        /* If so, follow __proto__ link to search prototype chain */
        proto_chain = JS_GetPrototype(cx, obj);

        /* TODO: No way to tell if the property doesn't exist in proto_chain
           or if it exists, but has an undefined value.  We assume the former. */
        if (proto_chain && JS_LookupProperty(cx, proto_chain, member_name, vp) &&
            (*vp != JSVAL_VOID))
            goto done;
    }

    /* Report lack of Java member with the given property name */
    JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, JSJMSG_NO_INSTANCE_NAME,
                         class_descriptor->name, member_name);
    return JS_FALSE;

done:
    /* Success.  Handle the multiple return values */
    if (java_wrapperp)
        *java_wrapperp = java_wrapper;
    if (member_descriptorp)
        *member_descriptorp = member_descriptor;
    return JS_TRUE;
}

JS_DLL_CALLBACK JSBool
JavaObject_getPropertyById(JSContext *cx, JSObject *obj, jsid id, jsval *vp)
{
    jobject java_obj;
    JavaMemberDescriptor *member_descriptor;
    JavaObjectWrapper *java_wrapper;
    JNIEnv *jEnv;
    JSObject *funobj;
    jsval field_val, method_val;
    JSBool success;

    /* printf("In JavaObject_getProperty\n"); */

    /* Get the Java per-thread environment pointer for this JSContext */
    jsj_MapJSContextToJSJThread(cx, &jEnv);
    if (!jEnv)
        return JS_FALSE;
        
    *vp = JSVAL_VOID;
    if (!lookup_member_by_id(cx, jEnv, obj, &java_wrapper, id, &member_descriptor, vp))
        return JS_FALSE;

    /* Handle access to special, non-Java properties of JavaObjects, e.g. the 
       "constructor" property of the prototype object */
    if (!member_descriptor)
        return JS_TRUE;

    java_obj = java_wrapper->java_obj;
    field_val = method_val = JSVAL_VOID;

    /* If a field member, get the value of the field */
    if (member_descriptor->field) {
        success = jsj_GetJavaFieldValue(cx, jEnv, member_descriptor->field, java_obj, &field_val);
        if (!success)
            return JS_FALSE;
    }

    /* If a method member, build a wrapper around the Java method */
    if (member_descriptor->methods) {
        /* Create a function object with this JavaObject as its parent, so that
           JSFUN_BOUND_METHOD binds it as the default 'this' for the function. */
        funobj = JS_CloneFunctionObject(cx, member_descriptor->invoke_func_obj, obj);
        if (!funobj)
            return JS_FALSE;
        method_val = OBJECT_TO_JSVAL(funobj);
    }

#if TEST_JAVAMEMBER
    /* Always create a JavaMember object, even though it's inefficient */
    obj = jsj_CreateJavaMember(cx, method_val, field_val);
    if (!obj)
        return JS_FALSE;
    *vp = OBJECT_TO_JSVAL(obj);
#else   /* !TEST_JAVAMEMBER */

    if (member_descriptor->field) {
        if (!member_descriptor->methods) {
            /* Return value of Java field */
            *vp = field_val;
        } else {
            /* Handle special case of access to a property that could refer
               to either a Java field or a method that share the same name.
               In Java, such ambiguity is not possible because the compiler 
               can statically determine which is being accessed. */
            obj = jsj_CreateJavaMember(cx, method_val, field_val);
            if (!obj)
                return JS_FALSE;
            *vp = OBJECT_TO_JSVAL(obj);
        }

    } else {
        /* Return wrapper around Java method */
        *vp = method_val;
    }

#endif  /* !TEST_JAVAMEMBER */

    return JS_TRUE;
}

JS_STATIC_DLL_CALLBACK(JSBool)
JavaObject_setPropertyById(JSContext *cx, JSObject *obj, jsid id, jsval *vp)
{
    jobject java_obj;
    const char *member_name;
    JavaObjectWrapper *java_wrapper;
    JavaClassDescriptor *class_descriptor;
    JavaMemberDescriptor *member_descriptor;
    jsval idval;
    JNIEnv *jEnv;

    /* printf("In JavaObject_setProperty\n"); */

    /* Get the Java per-thread environment pointer for this JSContext */
    jsj_MapJSContextToJSJThread(cx, &jEnv);
    if (!jEnv)
        return JS_FALSE;
    
    if (!lookup_member_by_id(cx, jEnv, obj, &java_wrapper, id, &member_descriptor, NULL))
        return JS_FALSE;

    /* Could be assignment to magic JS __proto__ property rather than a Java field */
    if (!member_descriptor) {
        JS_IdToValue(cx, id, &idval);
        if (!JSVAL_IS_STRING(idval))
            goto no_such_field;
        member_name = JS_GetStringBytes(JSVAL_TO_STRING(idval));
        if (strcmp(member_name, "__proto__"))
            goto no_such_field;
        if (!JSVAL_IS_OBJECT(*vp)) {
            JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                 JSJMSG_BAD_PROTO_ASSIGNMENT);
            return JS_FALSE;
        }
        JS_SetPrototype(cx, obj, JSVAL_TO_OBJECT(*vp));
        return JS_TRUE;
    }

    /* Check for the case where there is a method with the given name, but no field
       with that name */
    if (!member_descriptor->field)
        goto no_such_field;

    /* Silently fail if field value is final (immutable), as required by ECMA spec */
    if (member_descriptor->field->modifiers & ACC_FINAL)
        return JS_TRUE;
    
    java_obj = java_wrapper->java_obj;
    return jsj_SetJavaFieldValue(cx, jEnv, member_descriptor->field, java_obj, *vp);

no_such_field:
        JS_IdToValue(cx, id, &idval);
        member_name = JS_GetStringBytes(JSVAL_TO_STRING(idval));
        class_descriptor = java_wrapper->class_descriptor;
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                       JSJMSG_NO_NAME_IN_CLASS,
                       member_name, class_descriptor->name);
        return JS_FALSE;
}

static JSBool
JavaObject_lookupProperty(JSContext *cx, JSObject *obj, jsid id,
                         JSObject **objp, JSProperty **propp
#if defined JS_THREADSAFE && defined DEBUG
                            , const char *file, uintN line
#endif
                            )
{
    JNIEnv *jEnv;
    JSErrorReporter old_reporter;
    jsval dummy_val;

    /* printf("In JavaObject_lookupProperty()\n"); */
    
    /* Get the Java per-thread environment pointer for this JSContext */
    jsj_MapJSContextToJSJThread(cx, &jEnv);
    if (!jEnv)
        return JS_FALSE;

    old_reporter = JS_SetErrorReporter(cx, NULL);
    if (lookup_member_by_id(cx, jEnv, obj, NULL, id, NULL, &dummy_val)) {
        /* TODO - objp may not be set correctly if a property is found, not in
           obj, but somewhere in obj's proto chain.  However, there is
           no exported JS API to discover which object a property is defined
           in. */
        *objp = obj;
        *propp = (JSProperty*)1;
    } else {
        *objp = NULL;
        *propp = NULL;
    }

    JS_SetErrorReporter(cx, old_reporter);
    return JS_TRUE;
}

static JSBool
JavaObject_defineProperty(JSContext *cx, JSObject *obj, jsid id, jsval value,
                         JSPropertyOp getter, JSPropertyOp setter,
                         uintN attrs, JSProperty **propp)
{
    JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                    JSJMSG_JOBJECT_PROP_DEFINE);
    return JS_FALSE;
}

static JSBool
JavaObject_getAttributes(JSContext *cx, JSObject *obj, jsid id,
                        JSProperty *prop, uintN *attrsp)
{
    /* We don't maintain JS property attributes for Java class members */
    *attrsp = JSPROP_PERMANENT|JSPROP_ENUMERATE;
    return JS_FALSE;
}

static JSBool
JavaObject_setAttributes(JSContext *cx, JSObject *obj, jsid id,
                        JSProperty *prop, uintN *attrsp)
{
    /* We don't maintain JS property attributes for Java class members */
    if (*attrsp != (JSPROP_PERMANENT|JSPROP_ENUMERATE)) {
        JS_ASSERT(0);
        return JS_FALSE;
    }

    /* Silently ignore all setAttribute attempts */
    return JS_TRUE;
}

static JSBool
JavaObject_deleteProperty(JSContext *cx, JSObject *obj, jsid id, jsval *vp)
{
    JSVersion version = JS_GetVersion(cx);
    
    *vp = JSVAL_FALSE;

    if (!JSVERSION_IS_ECMA(version)) {
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                        JSJMSG_JOBJECT_PROP_DELETE);
        return JS_FALSE;
    } else {
        /* Attempts to delete permanent properties are silently ignored
           by ECMAScript. */
        return JS_TRUE;
    }
}

static JSBool
JavaObject_defaultValue(JSContext *cx, JSObject *obj, JSType type, jsval *vp)
{
    /* printf("In JavaObject_defaultValue()\n"); */
    return JavaObject_convert(cx, obj, type, vp);
}

static JSBool
JavaObject_newEnumerate(JSContext *cx, JSObject *obj, JSIterateOp enum_op,
                        jsval *statep, jsid *idp)
{
    JavaObjectWrapper *java_wrapper;
    JavaMemberDescriptor *member_descriptor;
    JavaClassDescriptor *class_descriptor;
    JNIEnv *jEnv;

    java_wrapper = JS_GetPrivate(cx, obj);
    /* Check for prototype object */
    if (!java_wrapper) {
        *statep = JSVAL_NULL;
        if (idp)
            *idp = INT_TO_JSVAL(0);
        return JS_TRUE;
    }

    class_descriptor = java_wrapper->class_descriptor;

    switch(enum_op) {
    case JSENUMERATE_INIT:
        
        /* Get the Java per-thread environment pointer for this JSContext */
        jsj_MapJSContextToJSJThread(cx, &jEnv);
        if (!jEnv)
            return JS_FALSE;

        member_descriptor = jsj_GetClassInstanceMembers(cx, jEnv, class_descriptor);
        *statep = PRIVATE_TO_JSVAL(member_descriptor);
        if (idp)
            *idp = INT_TO_JSVAL(class_descriptor->num_instance_members);
        return JS_TRUE;
        
    case JSENUMERATE_NEXT:
        member_descriptor = JSVAL_TO_PRIVATE(*statep);
        if (member_descriptor) {

            /* Don't enumerate explicit-signature methods, i.e. enumerate toValue,
               but not toValue(int), toValue(double), etc. */
            while (member_descriptor->methods && member_descriptor->methods->is_alias) {
                member_descriptor = member_descriptor->next;
                if (!member_descriptor) {
                    *statep = JSVAL_NULL;
                    return JS_TRUE;
                }
            }
            
            *idp = member_descriptor->id;
            *statep = PRIVATE_TO_JSVAL(member_descriptor->next);
            return JS_TRUE;
        }

        /* Fall through ... */

    case JSENUMERATE_DESTROY:
        *statep = JSVAL_NULL;
        return JS_TRUE;

    default:
        JS_ASSERT(0);
        return JS_FALSE;
    }
}

static JSBool
JavaObject_checkAccess(JSContext *cx, JSObject *obj, jsid id,
                      JSAccessMode mode, jsval *vp, uintN *attrsp)
{
    switch (mode) {
    case JSACC_WATCH:
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                                JSJMSG_JOBJECT_PROP_WATCH);
        return JS_FALSE;

    case JSACC_IMPORT:
        JS_ReportErrorNumber(cx, jsj_GetErrorMessage, NULL, 
                                                JSJMSG_JOBJECT_PROP_EXPORT);
        return JS_FALSE;

    default:
        return JS_TRUE;
    }
}

JSObjectOps JavaObject_ops = {
    /* Mandatory non-null function pointer members. */
    NULL,                       /* newObjectMap */
    NULL,                       /* destroyObjectMap */
    JavaObject_lookupProperty,
    JavaObject_defineProperty,
    JavaObject_getPropertyById, /* getProperty */
    JavaObject_setPropertyById, /* setProperty */
    JavaObject_getAttributes,
    JavaObject_setAttributes,
    JavaObject_deleteProperty,
    JavaObject_defaultValue,
    JavaObject_newEnumerate,
    JavaObject_checkAccess,

    /* Optionally non-null members start here. */
    NULL,                       /* thisObject */
    NULL,                       /* dropProperty */
    NULL,                       /* call */
    NULL,                       /* construct */
    NULL,                       /* xdrObject */
    NULL,                       /* hasInstance */
};

static JSObjectOps *
JavaObject_getObjectOps(JSContext *cx, JSClass *clazz)
{
    return &JavaObject_ops;
}

JSClass JavaObject_class = {
    "JavaObject", JSCLASS_HAS_PRIVATE,
    NULL, NULL, NULL, NULL,
    NULL, NULL, JavaObject_convert, JavaObject_finalize,
    JavaObject_getObjectOps,
};

extern JS_IMPORT_DATA(JSObjectOps) js_ObjectOps;


JSBool
jsj_init_JavaObject(JSContext *cx, JSObject *global_obj)
{
    JavaObject_ops.newObjectMap = js_ObjectOps.newObjectMap;
    JavaObject_ops.destroyObjectMap = js_ObjectOps.destroyObjectMap;

    if (!JS_InitClass(cx, global_obj, 
        0, &JavaObject_class, 0, 0,
        0, 0,
        0, 0))
        return JS_FALSE;

    return JS_TRUE;
}
