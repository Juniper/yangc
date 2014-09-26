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
#include "yang.h"
#include "yangparser.h"
#include "yangloader.h"
#include "yangstmt.h"

/*
 * Decide if a standard YANG statment is being used in a non-standard
 * way.  If it is, then we do not want to trigger the built-in behavior.
 * The test is whether the node and all its parents are in the YANG,
 * XSL, or SLAX namespaces.
 */
static int
yangStmtIgnore (slax_data_t *sdp)
{
    xmlNodePtr nodep;

    for (nodep = sdp->sd_ctxt->node; nodep; nodep = nodep->parent) {
	if (nodep->ns == NULL)
	    return TRUE;
	if (nodep->type != XML_ELEMENT_NODE)
	    break;

	const char *href = (const char *) nodep->ns->href;
	if (href == NULL)
	    return TRUE;
	if (streq((const char *) href, YIN_URI)
	    || streq((const char *) href, XSL_URI)
	    || streq((const char *) href, SLAX_URI))
	    continue;
	return TRUE;
    }

    return FALSE;
}

static int
yangStmtIsTop (slax_data_t *sdp UNUSED)
{
    /* XXX */
    return TRUE;
}

static int
yangStmtSetTopNamespaces (slax_data_t *sdp, yang_data_t *ydp)
{
    yang_stmt_t *pref_stmtp, *ns_stmtp;
    xmlNodePtr parent = sdp->sd_ctxt->node->parent;

    if (yangStmtIgnore(sdp))
	return 0;

    if (!yangStmtIsTop(sdp))
	return 0;

    pref_stmtp = yangStmtFind(NULL, YS_PREFIX);
    ns_stmtp = yangStmtFind(NULL, YS_NAMESPACE);

    char *namespace = yangStmtGetValue(sdp, parent, ns_stmtp);
    if (namespace == NULL)	/* Can't do anything without it */
	return 0;

    char *prefix = yangStmtGetValue(sdp, parent, pref_stmtp);

    slaxLog("yang: prefix '%s' for namespace '%s'",
	    prefix ?: "", namespace);

    xmlNodePtr rootp = ydp->yd_filep->yf_root;
    xmlNsPtr nsp;
    int seen_plain = FALSE, seen_prefix = FALSE;

    for (nsp = rootp->nsDef; nsp; nsp = nsp->next) {
	if (!streq(namespace, (const char *) nsp->href))
	    continue;
	if (nsp->prefix == NULL)
	    seen_plain = TRUE;
	else if (prefix && streq(prefix, (const char *) nsp->prefix))
	    seen_prefix = TRUE;

	if (seen_prefix && seen_plain)
	    break;
    }

    /* Add a default namespace that point to our URI */
    if (!seen_plain) {
	nsp = xmlNewNs(parent, (const xmlChar *) namespace, NULL);
    }

    /* Add a prefix that point to our URI */
    if (prefix && !seen_prefix) {
	nsp = xmlNewNs(parent, (const xmlChar *) namespace,
		       (const xmlChar *) prefix);
    }

    xmlFree(prefix);
    xmlFree(namespace);

    return 0;
}

static int
yangStmtSetArgPrefixOrNamespace (YANG_STMT_SETARG_ARGS)
{
    slaxLog("yang: arg: prefix %p %p", sdp, ysp);

    return yangStmtSetTopNamespaces(sdp, ydp);
}

static int
yangStmtSetArgModuleOrSubmodule (YANG_STMT_SETARG_ARGS)
{
    slaxLog("yang: arg: %s %p %p", ysp->ys_name, sdp, ysp);

    if (streq(ysp->ys_name, YS_MODULE))
	ydp->yd_filep->yf_flags |= YFF_MODULE;

    ydp->yd_filep->yf_main = sdp->sd_ctxt->node;
    return 0;
}

static int
yangStmtSetArgHelp (YANG_STMT_SETARG_ARGS)
{
    slaxLog("yang: arg: %s %p %p", ysp->ys_name, sdp, ysp);

    xmlNodePtr nodep = sdp->sd_ctxt->node;
    xmlDocPtr docp = nodep->doc;
    const xmlChar *name;

    if (docp != NULL && docp->dict != NULL)
	name = xmlDictLookup(docp->dict, (const xmlChar *) YS_DESCRIPTION, -1);
    else name = xmlStrdup((const xmlChar *) YS_DESCRIPTION);

    if (name) {
	if (docp == NULL || docp->dict == NULL
	    || xmlDictOwns(docp->dict, nodep->name) == 0)
	    xmlFree(const_drop(nodep->name));

	nodep->name = name;
	nodep->ns = ydp->yd_nsp;
    }

    return 0;
}

static int
yangStmtCloseType (YANG_STMT_CLOSE_ARGS)
{
    slaxLog("yang: type: %p %p", sdp, ysp);

    return 0;
}

static int
yangStmtCloseExtension (YANG_STMT_CLOSE_ARGS)
{
    xmlNodePtr nodep = sdp->sd_ctxt->node;
    yang_stmt_t *arg_stmtp = yangStmtFind(NULL, YS_ARGUMENT);
    char *element = yangStmtGetValue(sdp, nodep, arg_stmtp);
    char *name = slaxGetAttrib(nodep, YS_NAME);

    slaxLog("yang: extension: %p %p '%s' -> '%s'",
	    sdp, ysp, name, element ?: "");

    xmlFreeAndEasy(name);
    xmlFreeAndEasy(element);

    return 0;
}

/* YS_ANYXML */
static yang_relative_t ys_anyxml_children[] = {
    { YS_CONFIG, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_MANDATORY, NULL, 0 },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_ARGUMENT */
static yang_relative_t ys_argument_children[] = {
    { YS_YIN_ELEMENT, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_AUGMENT */
static yang_relative_t ys_augment_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CASE, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_BELONGS_TO */
static yang_relative_t ys_belongs_to_children[] = {
    { YS_PREFIX, NULL, YRF_MANDATORY },
    { NULL, NULL, 0 }
};

/* YS_BIT */
static yang_relative_t ys_bit_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_POSITION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_CASE */
static yang_relative_t ys_case_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_CHOICE */
static yang_relative_t ys_choice_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CASE, NULL, YRF_MULTIPLE },
    { YS_CONFIG, NULL, 0 },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DEFAULT, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_MANDATORY, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_CONTAINER */
static yang_relative_t ys_container_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONFIG, NULL, 0 },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_PRESENCE, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_DEVIATE */
static yang_relative_t ys_deviate_children[] = {
    { YS_CONFIG, NULL, 0 },
    { YS_DEFAULT, NULL, 0 },
    { YS_MANDATORY, NULL, 0 },
    { YS_MAX_ELEMENTS, NULL, 0 },
    { YS_MIN_ELEMENTS, NULL, 0 },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_TYPE, NULL, 0 },
    { YS_UNIQUE, NULL, YRF_MULTIPLE },
    { YS_UNITS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_DEVIATION */
static yang_relative_t ys_deviation_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_DEVIATE, NULL, YRF_MANDATORY | YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_ENUM */
static yang_relative_t ys_enum_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_VALUE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_EXTENSION */
static yang_relative_t ys_extension_children[] = {
    { YS_ARGUMENT, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_FEATURE */
static yang_relative_t ys_feature_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_GROUPING */
static yang_relative_t ys_grouping_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_IDENTITY */
static yang_relative_t ys_identity_children[] = {
    { YS_BASE, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_IMPORT */
static yang_relative_t ys_import_children[] = {
    { YS_PREFIX, NULL, 0 },
    { YS_REVISION_DATE, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_INCLUDE */
static yang_relative_t ys_include_children[] = {
    { YS_REVISION_DATE, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_INPUT */
static yang_relative_t ys_input_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_LEAF */
static yang_relative_t ys_leaf_children[] = {
    { YS_CONFIG, NULL, 0 },
    { YS_DEFAULT, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_MANDATORY, NULL, 0 },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPE, NULL, YRF_MANDATORY },
    { YS_UNITS, NULL, 0 },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_LEAF_LIST */
static yang_relative_t ys_leaf_list_children[] = {
    { YS_CONFIG, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_MAX_ELEMENTS, NULL, 0 },
    { YS_MIN_ELEMENTS, NULL, 0 },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_ORDERED_BY, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPE, NULL, YRF_MANDATORY },
    { YS_UNITS, NULL, 0 },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_LENGTH */
static yang_relative_t ys_length_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_ERROR_APP_TAG, NULL, 0 },
    { YS_ERROR_MESSAGE, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_LIST */
static yang_relative_t ys_list_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONFIG, NULL, 0 },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_KEY, NULL, 0 },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_MAX_ELEMENTS, NULL, 0 },
    { YS_MIN_ELEMENTS, NULL, 0 },
    { YS_MUST, NULL, YRF_MULTIPLE },
    { YS_ORDERED_BY, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_UNIQUE, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_MODULE */
static yang_relative_t ys_module_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_AUGMENT, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTACT, NULL, 0 },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_DEVIATION, NULL, YRF_MULTIPLE },
    { YS_EXTENSION, NULL, YRF_MULTIPLE },
    { YS_FEATURE, NULL, YRF_MULTIPLE },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IDENTITY, NULL, YRF_MULTIPLE },
    { YS_IMPORT, NULL, YRF_MULTIPLE },
    { YS_INCLUDE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_NAMESPACE, NULL, YRF_MANDATORY },
    { YS_NOTIFICATION, NULL, YRF_MULTIPLE },
    { YS_ORGANIZATION, NULL, 0 },
    { YS_PREFIX, NULL, YRF_MANDATORY },
    { YS_REFERENCE, NULL, YRF_MULTIPLE },
    { YS_REVISION, NULL, YRF_MULTIPLE },
    { YS_RPC, NULL, YRF_MULTIPLE },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_YANG_VERSION, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_MUST */
static yang_relative_t ys_must_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_ERROR_APP_TAG, NULL, 0 },
    { YS_ERROR_MESSAGE, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_NAMESPACE */
static yang_relative_t ys_namespace_children[] = {
    { YS_PREFIX, NULL, 0 },
    { YS_REVISION_DATE, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_NOTIFICATION */
static yang_relative_t ys_notification_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_OUTPUT */
static yang_relative_t ys_output_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_PATTERN */
static yang_relative_t ys_pattern_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_ERROR_APP_TAG, NULL, 0 },
    { YS_ERROR_MESSAGE, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_RANGE */
static yang_relative_t ys_range_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_ERROR_APP_TAG, NULL, 0 },
    { YS_ERROR_MESSAGE, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_REFINE */
static yang_relative_t ys_refine_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_REVISION */
static yang_relative_t ys_revision_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_RPC */
static yang_relative_t ys_rpc_children[] = {
    { YS_DESCRIPTION, NULL, 0 },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_INPUT, NULL, 0 },
    { YS_OUTPUT, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_SUBMODULE */
static yang_relative_t ys_submodule_children[] = {
    { YS_ANYXML, NULL, YRF_MULTIPLE },
    { YS_AUGMENT, NULL, YRF_MULTIPLE },
    { YS_BELONGS_TO, NULL, YRF_MANDATORY },
    { YS_CHOICE, NULL, YRF_MULTIPLE },
    { YS_CONTACT, NULL, 0 },
    { YS_CONTAINER, NULL, YRF_MULTIPLE },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_DEVIATION, NULL, YRF_MULTIPLE },
    { YS_EXTENSION, NULL, YRF_MULTIPLE },
    { YS_FEATURE, NULL, YRF_MULTIPLE },
    { YS_GROUPING, NULL, YRF_MULTIPLE },
    { YS_IDENTITY, NULL, YRF_MULTIPLE },
    { YS_IMPORT, NULL, YRF_MULTIPLE },
    { YS_INCLUDE, NULL, YRF_MULTIPLE },
    { YS_LEAF, NULL, YRF_MULTIPLE },
    { YS_LEAF_LIST, NULL, YRF_MULTIPLE },
    { YS_LIST, NULL, YRF_MULTIPLE },
    { YS_NOTIFICATION, NULL, YRF_MULTIPLE },
    { YS_ORGANIZATION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_REVISION, NULL, YRF_MULTIPLE },
    { YS_RPC, NULL, YRF_MULTIPLE },
    { YS_TYPEDEF, NULL, YRF_MULTIPLE },
    { YS_USES, NULL, YRF_MULTIPLE },
    { YS_YANG_VERSION, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_TYPE */
static yang_relative_t ys_type_children[] = {
    { YS_BIT, NULL, YRF_MULTIPLE },
    { YS_ENUM, NULL, YRF_MULTIPLE },
    { YS_FRACTION_DIGITS, NULL, 0 },
    { YS_LENGTH, NULL, 0 },
    { YS_PATH, NULL, 0 },
    { YS_PATTERN, NULL, YRF_MULTIPLE },
    { YS_RANGE, NULL, 0 },
    { YS_REQUIRE_INSTANCE, NULL, 0 },
    { YS_TYPE, NULL, YRF_MULTIPLE },
    { NULL, NULL, 0 }
};

/* YS_TYPEDEF */
static yang_relative_t ys_typedef_children[] = {
    { YS_DEFAULT, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_REFERENCE, NULL, 0 },
    { YS_STATUS, NULL, 0 },
    { YS_TYPE, NULL, YRF_MANDATORY },
    { YS_UNITS, NULL, 0 },
    { NULL, NULL, 0 }
};

/* YS_USES */
static yang_relative_t ys_uses_children[] = {
    { YS_AUGMENT, NULL, 0 },
    { YS_DESCRIPTION, NULL, 0 },
    { YS_IF_FEATURE, NULL, YRF_MULTIPLE },
    { YS_REFERENCE, NULL, 0 },
    { YS_REFINE, NULL, YRF_MULTIPLE },
    { YS_STATUS, NULL, 0 },
    { YS_WHEN, NULL, 0 },
    { NULL, NULL, 0 }
};

static yang_stmt_t yangStmtBuiltin[] = {
    { /* "anyxml" statement */
    .ys_name = YS_ANYXML,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_anyxml_children,
    },

    { /* "argument" statement */
    .ys_name = YS_ARGUMENT,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_argument_children,
    },

    { /* "augment" statement */
    .ys_name = YS_AUGMENT,
    .ys_argument = YS_TARGET_NODE,
    .ys_flags = 0,
    .ys_type = Y_TARGET,
    .ys_children = ys_augment_children,
    },

    { /* "base" statement */
    .ys_name = YS_BASE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "belongs-to" statement */
    .ys_name = YS_BELONGS_TO,
    .ys_argument = YS_MODULE,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_belongs_to_children,
    },

    { /* "bit" statement */
    .ys_name = YS_BIT,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_bit_children,
    },

    { /* "case" statement */
    .ys_name = YS_CASE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_case_children,
    },

    { /* "choice" statement */
    .ys_name = YS_CHOICE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_choice_children,
    },

    { /* "config" statement */
    .ys_name = YS_CONFIG,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_BOOLEAN,
    },

    { /* "contact" statement */
    .ys_name = YS_CONTACT,
    .ys_argument = YS_TEXT,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    },

    { /* "container" statement */
    .ys_name = YS_CONTAINER,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_container_children,
    },

    { /* "default" statement */
    .ys_name = YS_DEFAULT,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "description" statement */
    .ys_name = YS_DESCRIPTION,
    .ys_argument = YS_TEXT,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    },

    { /* "deviate" statement */
    .ys_name = YS_DEVIATE,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_DEVIATE,
    .ys_children = ys_deviate_children,
    },

    { /* "deviation" statement */
    .ys_name = YS_DEVIATION,
    .ys_argument = YS_TARGET_NODE,
    .ys_flags = 0,
    .ys_type = Y_TARGET,
    .ys_children = ys_deviation_children,
    },

    { /* "enum" statement */
    .ys_name = YS_ENUM,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_enum_children,
    },

    { /* "error-app-tag" statement */
    .ys_name = YS_ERROR_APP_TAG,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "error-message" statement */
    .ys_name = YS_ERROR_MESSAGE,
    .ys_argument = YS_VALUE,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    },

    { /* "extension" statement */
    .ys_name = YS_EXTENSION,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_extension_children,
    .ys_close = yangStmtCloseExtension,
    },

    { /* "feature" statement */
    .ys_name = YS_FEATURE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_feature_children,
    },

    { /* "fraction-digits" statement */
    .ys_name = YS_FRACTION_DIGITS,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_NUMBER,
    },

    { /* "grouping" statement */
    .ys_name = YS_GROUPING,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_grouping_children,
    },

    { /* "identity" statement */
    .ys_name = YS_IDENTITY,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_identity_children,
    },

    { /* "if-feature" statement */
    .ys_name = YS_IF_FEATURE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    },

    { /* "import" statement */
    .ys_name = YS_IMPORT,
    .ys_argument = YS_MODULE,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_import_children,
    },

    { /* "include" statement */
    .ys_name = YS_INCLUDE,
    .ys_argument = YS_MODULE,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_include_children,
    },

    { /* "input" statement */
    .ys_name = YS_INPUT,
    .ys_argument = NULL,
    .ys_flags = 0,
    .ys_type = Y_NONE,
    .ys_children = ys_input_children,
    },

    { /* "key" statement */
    .ys_name = YS_KEY,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "leaf" statement */
    .ys_name = YS_LEAF,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_leaf_children,
    },

    { /* "leaf-list" statement */
    .ys_name = YS_LEAF_LIST,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_leaf_list_children,
    },

    { /* "length" statement */
    .ys_name = YS_LENGTH,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_RANGE,
    .ys_children = ys_length_children,
    },

    { /* "list" statement */
    .ys_name = YS_LIST,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_list_children,
    },

    { /* "mandatory" statement */
    .ys_name = YS_MANDATORY,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_BOOLEAN,
    },

    { /* "max-elements" statement */
    .ys_name = YS_MAX_ELEMENTS,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_NUMBER,
    },

    { /* "min-elements" statement */
    .ys_name = YS_MIN_ELEMENTS,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_NUMBER,
    },

    { /* "module" statement */
    .ys_name = YS_MODULE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_module_children,
    .ys_setarg = yangStmtSetArgModuleOrSubmodule,
    },

    { /* "must" statement */
    .ys_name = YS_MUST,
    .ys_argument = YS_CONDITION,
    .ys_flags = 0,
    .ys_type = Y_XPATH,
    .ys_children = ys_must_children,
    },

    { /* "namespace" statement */
    .ys_name = YS_NAMESPACE,
    .ys_argument = YS_URI,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    .ys_children = ys_namespace_children,
    .ys_setarg = yangStmtSetArgPrefixOrNamespace,
    },

    { /* "notification" statement */
    .ys_name = YS_NOTIFICATION,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_notification_children,
    },

    { /* "ordered-by" statement */
    .ys_name = YS_ORDERED_BY,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_ORDERED,
    },

    { /* "organization" statement */
    .ys_name = YS_ORGANIZATION,
    .ys_argument = YS_TEXT,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    },

    { /* "output" statement */
    .ys_name = YS_OUTPUT,
    .ys_argument = NULL,
    .ys_flags = 0,
    .ys_type = Y_NONE,
    .ys_children = ys_output_children,
    },

    { /* "path" statement */
    .ys_name = YS_PATH,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_TARGET,
    },

    { /* "pattern" statement */
    .ys_name = YS_PATTERN,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    .ys_children = ys_pattern_children,
    },

    { /* "position" statement */
    .ys_name = YS_POSITION,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_REGEX,
    },

    { /* "prefix" statement */
    .ys_name = YS_PREFIX,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_setarg = yangStmtSetArgPrefixOrNamespace,
    },

    { /* "presence" statement */
    .ys_name = YS_PRESENCE,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "range" statement */
    .ys_name = YS_RANGE,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_RANGE,
    .ys_children = ys_range_children,
    },

    { /* "reference" statement */
    .ys_name = YS_REFERENCE,
    .ys_argument = YS_TEXT,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    },

    { /* "refine" statement */
    .ys_name = YS_REFINE,
    .ys_argument = YS_TARGET_NODE,
    .ys_flags = 0,
    .ys_type = Y_TARGET,
    .ys_children = ys_refine_children,
    },

    { /* "require-instance" statement */
    .ys_name = YS_REQUIRE_INSTANCE,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_BOOLEAN,
    },

    { /* "revision" statement */
    .ys_name = YS_REVISION,
    .ys_argument = YS_DATE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    .ys_children = ys_revision_children,
    },

    { /* "revision-date" statement */
    .ys_name = YS_REVISION_DATE,
    .ys_argument = YS_DATE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "rpc" statement */
    .ys_name = YS_RPC,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_rpc_children,
    },

    { /* "status" statement */
    .ys_name = YS_STATUS,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STATUS,
    },

    { /* "submodule" statement */
    .ys_name = YS_SUBMODULE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_submodule_children,
    .ys_setarg = yangStmtSetArgModuleOrSubmodule,
    },

    { /* "type" statement */
    .ys_name = YS_TYPE,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_type_children,
    .ys_close = yangStmtCloseType,
    },

    { /* "typedef" statement */
    .ys_name = YS_TYPEDEF,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_typedef_children,
    },

    { /* "unique" statement */
    .ys_name = YS_UNIQUE,
    .ys_argument = YS_TAG,
    .ys_flags = 0,
    .ys_type = Y_BOOLEAN,
    },

    { /* "units" statement */
    .ys_name = YS_UNITS,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "uses" statement */
    .ys_name = YS_USES,
    .ys_argument = YS_NAME,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    .ys_children = ys_uses_children,
    },

    { /* "value" statement */
    .ys_name = YS_VALUE,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "when" statement */
    .ys_name = YS_WHEN,
    .ys_argument = YS_CONDITION,
    .ys_flags = 0,
    .ys_type = Y_XPATH,
    },

    { /* "yang-version" statement */
    ys_name: YS_YANG_VERSION,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    },

    { /* "yin-element" statement */
    .ys_name = YS_YIN_ELEMENT,
    .ys_argument = YS_VALUE,
    .ys_flags = 0,
    .ys_type = Y_IDENT,
    },

    { /* End of list marker; do not remove */
    .ys_name = NULL,
    }
};

static yang_relative_t ys_yangc_children_parents[] = {
    { YS_EXTENSION, NULL, 0 },
    { NULL, NULL, 0 }
};

static yang_relative_t ys_yangc_parents_parents[] = {
    { YS_EXTENSION, NULL, 0 },
    { NULL, NULL, 0 }
};

static yang_relative_t ys_yangc_help_parents[] = {
    { YS_ANYXML, NULL, 0 },
    { YS_AUGMENT, NULL, 0 },
    { YS_BIT, NULL, 0 },
    { YS_CASE, NULL, 0 },
    { YS_CHOICE, NULL, 0 },
    { YS_CONTAINER, NULL, 0 },
    { YS_DEVIATION, NULL, 0 },
    { YS_ENUM, NULL, 0 },
    { YS_EXTENSION, NULL, 0 },
    { YS_FEATURE, NULL, 0 },
    { YS_GROUPING, NULL, 0 },
    { YS_IDENTITY, NULL, 0 },
    { YS_LEAF, NULL, 0 },
    { YS_LEAF_LIST, NULL, 0 },
    { YS_LENGTH, NULL, 0 },
    { YS_LIST, NULL, 0 },
    { YS_MODULE, NULL, 0 },
    { YS_MUST, NULL, 0 },
    { YS_NOTIFICATION, NULL, 0 },
    { YS_PATTERN, NULL, 0 },
    { YS_RANGE, NULL, 0 },
    { YS_REFINE, NULL, 0 },
    { YS_REVISION, NULL, 0 },
    { YS_RPC, NULL, 0 },
    { YS_SUBMODULE, NULL, 0 },
    { YS_TYPEDEF, NULL, 0 },
    { YS_USES, NULL, 0 },
    { NULL, NULL, 0 }
};

static yang_stmt_t yangStmtBuiltinExtensions[] = {
    { /* "children" statement */
    .ys_name = YS_CHILDREN,
    .ys_argument = YS_NAMES,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    .ys_parents = ys_yangc_children_parents,
    },
  
    { /* "parents" statement */
    .ys_name = YS_PARENTS,
    .ys_argument = YS_NAMES,
    .ys_flags = 0,
    .ys_type = Y_STRING,
    .ys_parents = ys_yangc_parents_parents,
    },

    { /* "help" statement */
    .ys_name = YS_HELP,
    .ys_argument = YS_TEXT,
    .ys_flags = YSF_YINELEMENT,
    .ys_type = Y_STRING,
    .ys_parents = ys_yangc_help_parents,
    .ys_setarg = yangStmtSetArgHelp,
    },

    { /* End of list marker; do not remove */
    .ys_name = NULL,
    }
};

void
yangStmtInitBuiltin (void)
{
    yangStmtAdd(yangStmtBuiltin, NULL, 0);
    yangStmtAdd(yangStmtBuiltinExtensions, YANGC_URI, 0);
}
