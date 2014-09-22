/*
 * Copyright (c) 2013, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 *
 * jsonwriter.c -- turn json-oriented XML into json text
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/queue.h>
#include <ctype.h>
#include <errno.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libslax/slax.h>
#include "slaxinternals.h"

#include "yang.h"
#include "yangstmt.h"

/* Forward declarations */
static int
yangWriteChildren (slax_writer_t *swp, xmlNodePtr parent,
		   const char *except, unsigned flags);

static int
yangWriteHasChildNodes (slax_writer_t *swp UNUSED, xmlNodePtr nodep)
{
    if (nodep == NULL)
	return FALSE;

    for (nodep = nodep->children; nodep; nodep = nodep->next)
	if (nodep->type != XML_TEXT_NODE)
	    return TRUE;

    return FALSE;
}

static const char *
yangWriteNeedsQuotes (slax_writer_t *swp UNUSED, const char *data)
{
    if (data == NULL)
	return "";

    if (strchr(data, (int) '\'') != NULL)
	return "\"";

    if (strchr(data, (int) '\"') != NULL)
	return "\'";

    size_t len = strlen(data);
    if (strcspn(data, "\"' \t\n\r") != len)
	return "\"";

    if (strstr(data, "//") || strstr(data, "/*"))
	return "\"";

    return "";
}

static int
yangWriteNode (slax_writer_t *swp, xmlNodePtr nodep, unsigned flags)
{
    const char *name = (const char *) nodep->name;
    const char *namespace = nodep->ns ? (const char *) nodep->ns->href : NULL;
    yang_stmt_t *ysp;
    const char *argument;
    int as_element;
    const char *data = NULL;
    int ignore_children = FALSE;

    ysp = yangStmtFind(namespace, name);
    if (ysp == NULL) {
	as_element = FALSE;
	argument = "argument";
    } else {
	as_element = (ysp->ys_flags & YSF_YINELEMENT) ? TRUE: FALSE;
	argument = ysp->ys_argument ?: "argument";
    }

    if (as_element) {
	ignore_children = TRUE;
	if (nodep) {
	    xmlNodePtr childp;
	    for (childp = nodep->children; childp; childp = childp->next) {
		if (childp->type != XML_ELEMENT_NODE
		    	|| childp->children == NULL)
		    continue;
		if (!streq(argument, (const char *) childp->name)) {
		    ignore_children = FALSE;
		    continue;
		}
		if (childp->children->type == XML_TEXT_NODE)
		    data = (const char *) childp->children->content;
		break;
	    }
	    if (childp && childp->next)
		ignore_children = FALSE;
	}
    } else {
	data = slaxGetAttrib(nodep, argument);
    }

    const char *quote = yangWriteNeedsQuotes(swp, data);

    /* XXX need to escape this data */
    slaxWrite(swp, "%s%s%s%s%s", name, data ? " " : "",
	      quote, data ?: "", quote);

    if (!ignore_children && yangWriteHasChildNodes(swp, nodep)) {
	slaxWrite(swp, " {");
	slaxWriteNewline(swp, NEWL_INDENT);

	yangWriteChildren(swp, nodep, argument, flags);

	slaxWrite(swp, "}");
	slaxWriteNewline(swp, NEWL_OUTDENT);
    } else {
	slaxWrite(swp, ";");
	slaxWriteNewline(swp, 0);
    }

    return 0;
}

static int
yangWriteChildren (slax_writer_t *swp, xmlNodePtr parent,
		   const char *except, unsigned flags)
{
    xmlNodePtr nodep;
    int rc = 0;

    for (nodep = parent->children; nodep; nodep = nodep->next) {
	if (nodep->type == XML_ELEMENT_NODE) {
	    if (except && streq(except, (const char *) nodep->name))
		continue;
	    yangWriteNode(swp, nodep, flags);
	}
    }

    return rc;
}

int
yangWriteDocNode (slaxWriterFunc_t func, void *data, xmlNodePtr nodep,
		       unsigned flags)
{
    slax_writer_t *swp = slaxGetWriter(func, data);
    int rc = yangWriteNode(swp, nodep, flags);

    slaxWriteNewline(swp, 0);

    slaxFreeWriter(swp);
    return rc;
}

int
yangWriteDoc (slaxWriterFunc_t func, void *data, xmlDocPtr docp,
	      unsigned flags)
{
    xmlNodePtr nodep = xmlDocGetRootElement(docp);
    return yangWriteDocNode(func, data, nodep, flags);
}
