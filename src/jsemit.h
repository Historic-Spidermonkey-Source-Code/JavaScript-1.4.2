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

#ifndef jsemit_h___
#define jsemit_h___
/*
 * JS bytecode generation.
 */

#include "jsstddef.h"
#include "jstypes.h"
#include "jsatom.h"
#include "jsopcode.h"
#include "jsprvtd.h"
#include "jspubtd.h"

JS_BEGIN_EXTERN_C

typedef enum JSStmtType {
    STMT_BLOCK        = 0,      /* compound statement: { s1[;... sN] } */
    STMT_LABEL        = 1,      /* labeled statement:  l: s */
    STMT_IF           = 2,      /* if (then) statement */
    STMT_ELSE         = 3,      /* else statement */
    STMT_SWITCH       = 4,      /* switch statement */
    STMT_WITH         = 5,      /* with statement */
    STMT_TRY	      = 6,	/* try statement */
    STMT_CATCH	      = 7,	/* catch block */
    STMT_FINALLY      = 8,	/* finally statement */
    STMT_DO_LOOP      = 9,      /* do/while loop statement */
    STMT_FOR_LOOP     = 10,     /* for loop statement */
    STMT_FOR_IN_LOOP  = 11,     /* for/in loop statement */
    STMT_WHILE_LOOP   = 12      /* while loop statement */
} JSStmtType;

#define STMT_IS_LOOP(stmt)      ((stmt)->type >= STMT_DO_LOOP)

typedef struct JSStmtInfo JSStmtInfo;

struct JSStmtInfo {
    JSStmtType      type;           /* statement type */
    ptrdiff_t       top;            /* offset of loop top from cg base */
    ptrdiff_t       update;         /* loop update offset (top if none) */
    ptrdiff_t       breaks;         /* offset of last break in loop */
    ptrdiff_t       continues;      /* offset of last continue in loop */
    JSAtom          *label;         /* label name if type is STMT_LABEL */
    JSStmtInfo      *down;          /* info for enclosing statement */
};

#define SET_STATEMENT_TOP(stmt, top) \
    ((stmt)->top = (stmt)->update = (top), (stmt)->breaks = (stmt)->continues = (-1))

struct JSTreeContext {              /* tree context for semantic checks */
    uint32          flags;          /* statement state flags, see below */
    uint32          tryCount;       /* total count of try statements parsed */
    JSStmtInfo      *topStmt;       /* top of statement info stack */
};

#define TCF_IN_FUNCTION 0x01        /* parsing inside function body */
#define TCF_RETURN_EXPR 0x02        /* function has 'return expr;' */
#define TCF_RETURN_VOID 0x04        /* function has 'return;' */
#define TCF_IN_FOR_INIT 0x08        /* parsing init expr of for; exclude 'in' */

#define TREE_CONTEXT_INIT(tc) \
    ((tc)->flags = 0, (tc)->tryCount = 0, (tc)->topStmt = NULL)

struct JSCodeGenerator {
    void            *codeMark;      /* low watermark in cx->codePool */
    void            *tempMark;      /* low watermark in cx->tempPool */
    jsbytecode      *base;          /* base of JS bytecode vector */
    jsbytecode      *limit;         /* one byte beyond end of bytecode */
    jsbytecode      *next;          /* pointer to next free bytecode */
    const char      *filename;      /* null or weak link to source filename */
    uintN           firstLine;      /* first line, for js_NewScriptFromCG */
    uintN           currentLine;    /* line number for tree-based srcnote gen */
    JSPrincipals    *principals;    /* principals for constant folding eval */
    JSTreeContext   treeContext;    /* for break/continue code generation */
    JSAtomList      atomList;       /* literals indexed for mapping */
    intN            stackDepth;     /* current stack depth in basic block */
    uintN           maxStackDepth;  /* maximum stack depth so far */
    jssrcnote       *notes;         /* source notes, see below */
    uintN           noteCount;      /* number of source notes so far */
    ptrdiff_t       lastNoteOffset; /* code offset for last source note */
    JSTryNote       *tryBase;       /* first exception handling note */
    JSTryNote       *tryNext;       /* next available note */
    size_t          tryNoteSpace;   /* # of bytes allocated at tryBase */
};

#define CG_CODE(cg,offset)  ((cg)->base + (offset))
#define CG_OFFSET(cg)       PTRDIFF((cg)->next, (cg)->base, jsbytecode)

/*
 * Initialize cg to allocate bytecode space from cx->codePool, and srcnote
 * space from cx->tempPool.  Return true on success.  Report an error and
 * return false if the initial code segment can't be allocated.
 */
extern JS_FRIEND_API(JSBool)
js_InitCodeGenerator(JSContext *cx, JSCodeGenerator *cg,
		     const char *filename, uintN lineno,
		     JSPrincipals *principals);

/*
 * Release cx->codePool and cx->tempPool to marks set by js_InitCodeGenerator.
 * Note that cgs are magic: they own tempPool and codePool "tops-of-stack" 
 * above their codeMark and tempMark points.  This means you cannot alloc
 * from tempPool and save the pointer beyond the next JS_FinishCodeGenerator.
 */
extern JS_FRIEND_API(void)
js_FinishCodeGenerator(JSContext *cx, JSCodeGenerator *cg);

/*
 * Emit one bytecode.
 */
extern ptrdiff_t
js_Emit1(JSContext *cx, JSCodeGenerator *cg, JSOp op);

/*
 * Emit two bytecodes, an opcode (op) with a byte of immediate operand (op1).
 */
extern ptrdiff_t
js_Emit2(JSContext *cx, JSCodeGenerator *cg, JSOp op, jsbytecode op1);

/*
 * Emit three bytecodes, an opcode with two bytes of immediate operands.
 */
extern ptrdiff_t
js_Emit3(JSContext *cx, JSCodeGenerator *cg, JSOp op, jsbytecode op1,
	 jsbytecode op2);

/*
 * Emit (1 + extra) bytecodes, for N bytes of op and its immediate operand.
 */
extern ptrdiff_t
js_EmitN(JSContext *cx, JSCodeGenerator *cg, JSOp op, size_t extra);

/*
 * Unsafe macro to call js_SetJumpOffset and return false if it does.
 */
#define CHECK_AND_SET_JUMP_OFFSET(cx,cg,pc,off)                               \
    JS_BEGIN_MACRO                                                            \
	if (!js_SetJumpOffset(cx, cg, pc, off))                               \
	    return JS_FALSE;                                                  \
    JS_END_MACRO

#define CHECK_AND_SET_JUMP_OFFSET_AT(cx,cg,off)                               \
    CHECK_AND_SET_JUMP_OFFSET(cx, cg, CG_CODE(cg,off), CG_OFFSET(cg) - (off))

extern JSBool
js_SetJumpOffset(JSContext *cx, JSCodeGenerator *cg, jsbytecode *pc,
		 ptrdiff_t off);

/*
 * Push the C-stack-allocated struct at stmt onto the stmtInfo stack.
 */
extern void
js_PushStatement(JSTreeContext *tc, JSStmtInfo *stmt, JSStmtType type,
		 ptrdiff_t top);

/*
 * Emit a break instruction, recording it for backpatching.
 */
extern ptrdiff_t
js_EmitBreak(JSContext *cx, JSCodeGenerator *cg, JSStmtInfo *stmt,
	     JSAtomListElement *label);

/*
 * Emit a continue instruction, recording it for backpatching.
 */
extern ptrdiff_t
js_EmitContinue(JSContext *cx, JSCodeGenerator *cg, JSStmtInfo *stmt,
		JSAtomListElement *label);

/*
 * Pop tc->topStmt.  If the top JSStmtInfo struct is not stack-allocated, it
 * is up to the caller to free it.
 */
extern void
js_PopStatement(JSTreeContext *tc);

/*
 * Like js_PopStatement(&cg->treeContext), also patch breaks and continues.
 * May fail if a jump offset overflows.
 */
extern JSBool
js_PopStatementCG(JSContext *cx, JSCodeGenerator *cg);

/*
 * Emit code into cg for the tree rooted at pn.
 */
extern JSBool
js_EmitTree(JSContext *cx, JSCodeGenerator *cg, JSParseNode *pn);

/*
 * Emit code into cg for the tree rooted at body, then create a persistent
 * script for fun from cg.
 */
extern JSBool
js_EmitFunctionBody(JSContext *cx, JSCodeGenerator *cg, JSParseNode *body,
		    JSFunction *fun);

/*
 * Source notes generated along with bytecode for decompiling and debugging.
 * A source note is a uint8 with 5 bits of type and 3 of offset from the pc of
 * the previous note.  If 3 bits of offset aren't enough, extended delta notes
 * (SRC_XDELTA) consisting of 2 set high order bits followed by 6 offset bits
 * are emitted before the next note.  Some notes have operand offsets encoded
 * immediately after them, in note bytes or byte-triples.
 *
 * At most one "gettable" note (i.e., a note of type other than SRC_NEWLINE,
 * SRC_SETLINE, and SRC_XDELTA) applies to a given bytecode.
 *
 * NB: the js_SrcNoteName and js_SrcNoteArity arrays in jsemit.c are indexed
 * by this enum, so their initializers need to match the order here.
 */
typedef enum JSSrcNoteType {
    SRC_NULL        = 0,        /* terminates a note vector */
    SRC_IF          = 1,        /* JSOP_IFEQ bytecode is from an if-then */
    SRC_IF_ELSE     = 2,        /* JSOP_IFEQ bytecode is from an if-then-else */
    SRC_WHILE       = 3,        /* JSOP_IFEQ is from a while loop */
    SRC_FOR         = 4,        /* JSOP_NOP or JSOP_POP in for loop head */
    SRC_CONTINUE    = 5,        /* JSOP_GOTO is a continue, not a break;
                                   also used on JSOP_ENDINIT if extra comma
                                   at end of array literal: [1,2,,] */
    SRC_VAR         = 6,        /* JSOP_NAME/FORNAME with a var declaration */
    SRC_PCDELTA     = 7,        /* offset from comma-operator to next POP,
				   or from CONDSWITCH to first CASE opcode */
    SRC_ASSIGNOP    = 8,        /* += or another assign-op follows */
    SRC_COND        = 9,        /* JSOP_IFEQ is from conditional ?: operator */
    SRC_PAREN       = 10,       /* JSOP_NOP generated to mark user parens */
    SRC_HIDDEN      = 11,       /* opcode shouldn't be decompiled */
    SRC_PCBASE      = 12,       /* offset of first obj.prop.subprop bytecode */
    SRC_LABEL       = 13,       /* JSOP_NOP for label: with atomid immediate */
    SRC_LABELBRACE  = 14,       /* JSOP_NOP for label: {...} begin brace */
    SRC_ENDBRACE    = 15,       /* JSOP_NOP for label: {...} end brace */
    SRC_BREAK2LABEL = 16,       /* JSOP_GOTO for 'break label' with atomid */
    SRC_CONT2LABEL  = 17,       /* JSOP_GOTO for 'continue label' with atomid */
    SRC_SWITCH      = 18,       /* JSOP_*SWITCH with offset to end of switch,
				   2nd off to first JSOP_CASE if condswitch */
    SRC_FUNCDEF     = 19,       /* JSOP_NOP for function f() with atomid */
    SRC_TRYFIN	    = 20,       /* JSOP_NOP for try or finally section */
    SRC_CATCH       = 21,       /* catch block has guard */
    SRC_NEWLINE     = 22,       /* bytecode follows a source newline */
    SRC_SETLINE     = 23,       /* a file-absolute source line number note */
    SRC_XDELTA      = 24        /* 24-31 are for extended delta notes */
} JSSrcNoteType;

#define SN_TYPE_BITS            5
#define SN_DELTA_BITS           3
#define SN_XDELTA_BITS          6
#define SN_TYPE_MASK            (JS_BITMASK(SN_TYPE_BITS) << SN_DELTA_BITS)
#define SN_DELTA_MASK           ((ptrdiff_t)JS_BITMASK(SN_DELTA_BITS))
#define SN_XDELTA_MASK          ((ptrdiff_t)JS_BITMASK(SN_XDELTA_BITS))

#define SN_MAKE_NOTE(sn,t,d)    (*(sn) = (jssrcnote)                          \
					  (((t) << SN_DELTA_BITS)             \
					   | ((d) & SN_DELTA_MASK)))
#define SN_MAKE_XDELTA(sn,d)    (*(sn) = (jssrcnote)                          \
					  ((SRC_XDELTA << SN_DELTA_BITS)      \
					   | ((d) & SN_XDELTA_MASK)))

#define SN_IS_XDELTA(sn)        ((*(sn) >> SN_DELTA_BITS) >= SRC_XDELTA)
#define SN_TYPE(sn)             (SN_IS_XDELTA(sn) ? SRC_XDELTA                \
						  : *(sn) >> SN_DELTA_BITS)
#define SN_SET_TYPE(sn,type)    SN_MAKE_NOTE(sn, type, SN_DELTA(sn))
#define SN_IS_GETTABLE(sn)      (SN_TYPE(sn) < SRC_NEWLINE)

#define SN_DELTA(sn)            ((ptrdiff_t)(SN_IS_XDELTA(sn)                 \
					     ? *(sn) & SN_XDELTA_MASK         \
					     : *(sn) & SN_DELTA_MASK))
#define SN_SET_DELTA(sn,delta)  (SN_IS_XDELTA(sn)                             \
				 ? SN_MAKE_XDELTA(sn, delta)                  \
				 : SN_MAKE_NOTE(sn, SN_TYPE(sn), delta))

#define SN_DELTA_LIMIT          ((ptrdiff_t)JS_BIT(SN_DELTA_BITS))
#define SN_XDELTA_LIMIT         ((ptrdiff_t)JS_BIT(SN_XDELTA_BITS))

/*
 * Offset fields follow certain notes and are frequency-encoded: an offset in
 * [0,0x7f] consumes one byte, an offset in [0x80,0x7fffff] takes three, and
 * the high bit of the first byte is set.
 */
#define SN_3BYTE_OFFSET_FLAG    0x80
#define SN_3BYTE_OFFSET_MASK    0x7f

extern JS_FRIEND_DATA(const char *) js_SrcNoteName[];
extern JS_FRIEND_DATA(uint8) js_SrcNoteArity[];
extern JS_FRIEND_API(uintN) js_SrcNoteLength(jssrcnote *sn);

#define SN_LENGTH(sn)           ((js_SrcNoteArity[SN_TYPE(sn)] == 0) ? 1      \
				 : js_SrcNoteLength(sn))
#define SN_NEXT(sn)             ((sn) + SN_LENGTH(sn))

/* A source note array is terminated by an all-zero element. */
#define SN_MAKE_TERMINATOR(sn)  (*(sn) = SRC_NULL)
#define SN_IS_TERMINATOR(sn)    (*(sn) == SRC_NULL)

/*
 * Append a new source note of the given type (and therefore size) to cg's
 * notes dynamic array, updating cg->noteCount.  Return the new note's index
 * within the array pointed at by cg->notes.  Return -1 if out of memory.
 */
extern intN
js_NewSrcNote(JSContext *cx, JSCodeGenerator *cg, JSSrcNoteType type);

extern intN
js_NewSrcNote2(JSContext *cx, JSCodeGenerator *cg, JSSrcNoteType type,
	       ptrdiff_t offset);

extern intN
js_NewSrcNote3(JSContext *cx, JSCodeGenerator *cg, JSSrcNoteType type,
	       ptrdiff_t offset1, ptrdiff_t offset2);

/*
 * Get and set the offset operand identified by which (0 for the first, etc.).
 */
extern JS_FRIEND_API(ptrdiff_t)
js_GetSrcNoteOffset(jssrcnote *sn, uintN which);

extern JSBool
js_SetSrcNoteOffset(JSContext *cx, JSCodeGenerator *cg, uintN index,
		    uintN which, ptrdiff_t offset);

/*
 * Finish taking source notes in cx's tempPool by copying them to new
 * stable store allocated via JS_malloc.  Return null on malloc failure,
 * which means this function reported an error.
 */
extern jssrcnote *
js_FinishTakingSrcNotes(JSContext *cx, JSCodeGenerator *cg);

/*
 * Allocate cg->treeContext.tryCount notes (plus one for the end sentinel)
 * from cx->tempPool and set up cg->tryBase/tryNext for exactly tryCount
 * js_NewTryNote calls.  The storage is freed by js_FinishCodeGenerator.
 */
extern JSBool
js_AllocTryNotes(JSContext *cx, JSCodeGenerator *cg);

/*
 * Grab the next trynote slot in cg, filling it in appropriately.
 */
extern JSTryNote *
js_NewTryNote(JSContext *cx, JSCodeGenerator *cg, ptrdiff_t start,
	      ptrdiff_t end, ptrdiff_t catchStart);

/*
 * Finish generating exception information, and copy it to JS_malloc
 * storage.
 */
extern JSBool
js_FinishTakingTryNotes(JSContext *cx, JSCodeGenerator *cg, JSTryNote **tryp);

JS_END_EXTERN_C

#endif /* jsemit_h___ */
