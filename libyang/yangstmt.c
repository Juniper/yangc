/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#include <ctype.h>
#include <sys/queue.h>
#include <errno.h>
#include <sys/param.h>

#include <libxslt/extensions.h>
#include <libxslt/documents.h>
#include <libexslt/exslt.h>

#include "yanginternals.h"
#include <libslax/slax.h>
#include <libslax/slaxdata.h>
#include <libslax/internal/slaxutil.h>
#include <libyang/yang.h>
#include <libyang/yangparser.h>
#include <libyang/yangloader.h>
#include <libyang/yangstmt.h>

static yang_stmt_list_t yangStmtList;


static void
yangStmtRebuildParents (yang_stmt_t *ysp, yang_relative_t *yrp)
{
    yang_stmt_t *parent = yangStmtFind(yrp->yr_namespace, yrp->yr_name);
    yang_relative_t *tmp;
    int count = 0;

    if (parent == NULL) {
	slaxLog("could not add statement: not found '%s:%s'",
		yrp->yr_namespace ?: "", yrp->yr_name);
	return;
    }

    for (tmp = parent->ys_children; tmp && tmp->yr_name; tmp++)
	count += 1;

    yang_relative_t *newp = xmlMalloc((count + 2) * sizeof(*newp));
    if (newp == NULL) {
	slaxLog("out of memory for '%s'", ysp->ys_name);
	return;
    }

    memcpy(newp, parent->ys_children, count * sizeof(*newp));
    tmp = newp + count;
    tmp->yr_name = ysp->ys_name;
    tmp->yr_namespace = ysp->ys_namespace;
    tmp->yr_flags = yrp->yr_flags;
    bzero(tmp + 1, sizeof(*newp));

    if (parent->ys_flags & YSF_CHILDREN_ALLOCED)
	xmlFreeAndEasy(parent->ys_children);
    parent->ys_children = newp;
    parent->ys_flags |= YSF_CHILDREN_ALLOCED;
}

/**
 * Add a new statement to the list of supported statements
 */
void
yangStmtAdd (yang_stmt_t *ysp, const char *namespace, int count)
{
    static unsigned ys_id;

    if (count <= 0)
	count = INT_MAX;

    for ( ; count > 0 && ysp->ys_name; count--, ysp++) {
	ysp->ys_id = ys_id++;
	ysp->ys_namespace = namespace;

	if (ysp->ys_parents) {
	    yang_relative_t *yrp;

	    for (yrp = ysp->ys_parents; yrp && yrp->yr_name; yrp++) {
		yangStmtRebuildParents(ysp, yrp);
	    }
	}

	TAILQ_INSERT_TAIL(&yangStmtList, ysp, ys_link);
    }
}

yang_stmt_t *
yangStmtFind (const char *namespace, const char *name)
{
    yang_stmt_t *ysp;
    int is_yin = namespace ? streq(namespace, YIN_URI) : FALSE;

    TAILQ_FOREACH(ysp, &yangStmtList, ys_link) {
	if (namespace) {
	    if (ysp->ys_namespace) {
		if (!streq(namespace, ysp->ys_namespace))
		    continue;
	    } else if (!is_yin)
		continue;
	}

	if (streq(ysp->ys_name, name))
	    return ysp;
    }

    return NULL;
}

static xmlNsPtr
yangStmtFindNs (yang_data_t *ydp, yang_stmt_t *ysp)
{
    if (ysp->ys_namespace == NULL)
	return ydp->yd_nsp;

    return NULL;
}

char *
yangStmtGetValueName (slax_data_t *sdp, xmlNodePtr nodep,
		      const char *namespace, const char *name,
		      const char *argument, unsigned flags)
{
    if (nodep == NULL) {
	nodep = sdp->sd_ctxt->node;
	if (nodep == NULL)
	    return NULL;
    }

    for (nodep = nodep->children; nodep; nodep = nodep->next) {
	if (nodep->type != XML_ELEMENT_NODE)
	    continue;
	
	if (namespace) {
	    if (nodep->ns == NULL
		|| !streq(namespace, (const char *) nodep->ns->href))
		continue;
	}

	if (!streq(name, (const char *) nodep->name))
	    continue;

	if (flags & YSF_YINELEMENT) {
	    xmlNodePtr childp;
	    for (childp = nodep->children; childp; childp = childp->next) {
		if (childp->type != XML_ELEMENT_NODE
		    	|| childp->children == NULL)
		    continue;
		if (!streq(argument, (const char *) childp->name))
		    continue;
		if (childp->children->type == XML_TEXT_NODE)
		    return (char *) xmlStrdup(childp->children->content);
		break;
	    }

	} else {
	    return slaxGetAttrib(nodep, argument);
	}
    }

    return NULL;
}

char *
yangStmtGetValue (slax_data_t *sdp, xmlNodePtr nodep, yang_stmt_t *ysp)
{
    if (ysp == NULL)
	return NULL;

    return yangStmtGetValueName(sdp, nodep, ysp->ys_namespace,
				ysp->ys_name, ysp->ys_argument,
				ysp->ys_flags);
}

static yang_relative_t *
yangFindRelative (yang_relative_t *listp, yang_stmt_t *ysp)
{
    for ( ; listp->yr_name; listp++)
	if (streq(listp->yr_name, ysp->ys_name))
	    return listp;

    return NULL;
}

static int
yangSeenTestAndSet (yang_parse_stack_t *ypsp, yang_stmt_t *ysp)
{
    unsigned x = ysp->ys_id / NBBY;
    unsigned y = ysp->ys_id % NBBY;
    unsigned z = 1 << y;
    yang_seen_elt_t *map = ypsp->yps_seen.yss_map;

    slaxLog("yangSeenTestAndSet: %d -> %d/%d/%d (%p)",
	    ysp->ys_id, x, y, z, map);

    int res = (map[x] & z) ? 1 : 0;

    map[x] |= z;
    return res;
}

static unsigned
yangCheckChildren (slax_data_t *sdp, yang_stmt_t *ysp, const char *name)
{
    yang_data_t *ydp = yangData(sdp);
    yang_parse_stack_t *ypsp = ydp->yd_stackp;
    yang_stmt_t *parent = ypsp ? ypsp->yps_stmt : NULL;

    slaxLog("check child: %s", name);

    /*
     * If we're under a 'template', then we can't know what our eventual
     * parent will be.  Skip this check.
     */
    xmlNodePtr nodep = sdp->sd_ctxt->node->parent;
    if (nodep && nodep->type == XML_ELEMENT_NODE) {
	if (slaxNodeIsXsl(nodep, ELT_TEMPLATE))
	    return 0;
    }

    if (ysp && parent && parent->ys_children) {
	yang_relative_t *yrp = yangFindRelative(parent->ys_children, ysp);
	if (yrp == NULL) {
	    yangError(sdp, "statement '%s' cannot contain statement '%s'",
		      parent->ys_name, name);
	    return YPSF_DISCARD;
	} else if (!(yrp->yr_flags & YRF_MULTIPLE)
		   && yangSeenTestAndSet(ypsp, ysp)) {
	    yangError(sdp, "statement '%s' can only contain "
		      "one statement '%s'",
		      parent->ys_name, name);
	    return YPSF_DISCARD;
	}
    }

    return 0;
}

void
yangStmtOpen (slax_data_t *sdp, const char *raw_name)
{
    yang_data_t *ydp = yangData(sdp);
    unsigned flags = 0;
    char *local_name, *ns = NULL;
    const char *name = raw_name;

    local_name = strchr(name, ':');
    if (local_name) {
	size_t len = local_name - name;
	if (len) {	    
	    ns = alloca(len + 1);
	    memcpy(ns, name, len);
	    ns[len] = '\0';
	}
	name = local_name + 1;
    }

    slaxLog("yang: open: %s (%s:%s)", raw_name, ns ?: "--", name);
 
    slaxElementOpen(sdp, name);

    yang_stmt_t *ysp = yangStmtFind(ns, name);
    if (ysp) {
	sdp->sd_ctxt->node->ns = yangStmtFindNs(ydp, ysp);

	flags |= yangCheckChildren(sdp, ysp, name);

	if (ysp->ys_open) {
	    slaxLog("yang: calling open for %s", name);
	    ysp->ys_open(sdp, ydp, ysp);
	}

	if (ysp->ys_type)
	    sdp->sd_ytype = sdp->sd_ttype = ysp->ys_type;
    } else {
	slaxError("%s:%d: unknown statement: %s",
		  sdp->sd_filename, sdp->sd_line, raw_name);
	sdp->sd_errors += 1;
    }

    if (sdp->sd_ttype == 0)
	sdp->sd_ytype = sdp->sd_ttype = Y_STRING;

    /* Tell the lexer we are looking for a string */
    if (sdp->sd_ytype == Y_STRING || sdp->sd_ytype == Y_IDENT
	|| sdp->sd_ytype == Y_REGEX)
	sdp->sd_flags |= SDF_STRING;

    /* Allocate a frame on the parse stack */
    ydp->yd_stackp = ydp->yd_stackp ? ydp->yd_stackp + 1 : ydp->yd_stack;

    /* Fill in the parse stack frame with the info we know */
    bzero(ydp->yd_stackp, sizeof(*ydp->yd_stackp));
    ydp->yd_stackp->yps_stmt = ysp;
    ydp->yd_stackp->yps_flags = flags;
}

void
yangStmtClose (slax_data_t *sdp, const char *name)
{
    yang_data_t *ydp = yangData(sdp);

    slaxLog("yang: close: %s", name);

    yang_stmt_t *ysp = ydp->yd_stackp ? ydp->yd_stackp->yps_stmt : NULL;
    if (ysp && ysp->ys_close) {
	slaxLog("yang: calling close for %s", name);
	ysp->ys_close(sdp, ydp, ysp);
    }

    unsigned flags = 0;

    if (ydp->yd_stackp) {
	flags = ydp->yd_stackp->yps_flags;
	ydp->yd_stackp->yps_stmt = NULL;

	if (ydp->yd_stackp == ydp->yd_stack)
	    ydp->yd_stackp = NULL;
	else
	    ydp->yd_stackp -= 1;
    }

    xmlNodePtr nodep = sdp->sd_ctxt->node;
    slaxElementClose(sdp);

    if (nodep && (flags & YPSF_DISCARD)) {
	slaxLog("yang: close: discarding '%s'", (const char *) nodep->name);
	xmlUnlinkNode(nodep);
    }
}

void
yangStmtSetArgument (slax_data_t *sdp, slax_string_t *value,
		     int is_xpath UNUSED)
{
    yang_data_t *ydp = yangData(sdp);
    xmlNodePtr nodep = sdp->sd_ctxt->node;
    const char *name = (const char *) nodep->name;
    yang_stmt_t *ysp;
    const char *argument;
    int as_element;

    slaxLog("yangStmtSetArgument: %x %x %d -> %x:%s",
	    sdp, value, is_xpath, nodep, name);

    ysp = ydp->yd_stackp ? ydp->yd_stackp->yps_stmt : NULL;
    if (ysp == NULL) {
	as_element = FALSE;
	argument = "argument";
    } else {
	as_element = (ysp->ys_flags & YSF_YINELEMENT) ? TRUE: FALSE;
	argument = ysp->ys_argument;

	if (argument == NULL) {
	    xmlParserError(sdp->sd_ctxt,
			   "%s:%d: statement '%s' does not accept "
			   "an argument ('%s')",
			   sdp->sd_filename, sdp->sd_line,
			   name, value->ss_token);
	    return;
	}
    }

    if (as_element) {
	slaxElementOpen(sdp, argument);
	slaxElementXPath(sdp, value, as_element, FALSE);
	slaxElementClose(sdp);

    } else {
	slaxAttribAddXpath(sdp, argument, value);
    }

    if (ysp && ysp->ys_setarg)
	ysp->ys_setarg(sdp, ydp, ysp);
}

void
yangStmtCheckArgument (slax_data_t *sdp, slax_string_t *sp)
{
    yang_data_t *ydp = yangData(sdp);
    yang_stmt_t *ysp = ydp->yd_stackp ? ydp->yd_stackp->yps_stmt : NULL;

    if (ysp && ysp->ys_argument && sp == NULL) {
	slaxError("%s:%d: missing argument for %s",
		  sdp->sd_filename, sdp->sd_line, ysp->ys_name);
	sdp->sd_errors += 1;
    }
}

static int
yangIsSimple (slax_string_t *ssp)
{
    return (ssp->ss_ttype == T_BARE || ssp->ss_ttype == T_QUOTED
	    || ssp->ss_ttype == T_NUMBER
	    || (ssp->ss_ttype > V_LAST && ssp->ss_ttype  < L_LAST));
}

static slax_string_t *
yangPadString (slax_string_t *val, int after)
{
    int len1 = strlen(val->ss_token);
    int len = len1 + 1;
    int start = after ? 0 : 1;
    slax_string_t *ssp = xmlMalloc(sizeof(*ssp) + len + 1);

    if (ssp == NULL)
	return NULL;

    if (!after)
	ssp->ss_token[0] = ' ';
    memcpy(ssp->ss_token + start, val->ss_token, len1);
    if (after)
	ssp->ss_token[len1] = ' ';
    ssp->ss_token[len] = '\0';
    ssp->ss_next = ssp->ss_concat = NULL;
    ssp->ss_ttype = T_QUOTED;
    ssp->ss_flags = val->ss_flags;

    return ssp;
}

slax_string_t *
yangConcatValues (slax_data_t *sdp, slax_string_t *one,
		  slax_string_t *two, int with_space)
{
    slax_string_t *ssp;

    /*
     * First we need to decide if these strings need simple concatenation
     */
    if (yangIsSimple(one) && yangIsSimple(two)) {
	int len1 = strlen(one->ss_token);
	int len2 = strlen(two->ss_token);
	int len = len1 + len2;

	if (with_space)
	    len += 1;

	ssp = xmlMalloc(sizeof(*ssp) + len + 1);
	if (ssp == NULL)
	    return NULL;

	memcpy(ssp->ss_token, one->ss_token, len1);
	if (with_space)
	    ssp->ss_token[len1++] = ' ';
	memcpy(ssp->ss_token + len1, two->ss_token, len2);
	ssp->ss_token[len] = '\0';
	ssp->ss_next = ssp->ss_concat = NULL;
	ssp->ss_ttype = T_QUOTED;
	ssp->ss_flags = one->ss_flags;
	slaxLog("yangConcatValues: built %p %d:'%s'",
		ssp, ssp->ss_ttype,ssp->ss_token);

	slaxStringFree(one);
	slaxStringFree(two);

	return ssp;
    }

    if (with_space) {
	if (yangIsSimple(one)) {
	    ssp = yangPadString(one, TRUE);
	    if (ssp) {
		slaxStringFree(one);
		one = ssp;
	    }

	} else if (yangIsSimple(two)) {
	    ssp = yangPadString(two, FALSE);
	    if (ssp) {
		slaxStringFree(two);
		two = ssp;
	    }
	} else {
	    slax_string_t *spacep = slaxStringLiteral(" ", T_QUOTED);
	    ssp = slaxStringLiteral("_", L_UNDERSCORE);
	    one = slaxConcatRewrite(sdp, one, ssp, spacep);
	}
    }

    ssp = slaxStringLiteral("_", L_UNDERSCORE);
    ssp = slaxConcatRewrite(sdp, one, ssp, two);
    return ssp;
}

void
yangStmtInit (void)
{
    TAILQ_INIT(&yangStmtList);
    yangStmtInitBuiltin();
}
