/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#include <ctype.h>
#include <sys/queue.h>
#include <errno.h>

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

static slax_data_list_t yang_features;
static int yang_features_initted;

void
yangFeatureAdd (const char *feature_name)
{
    if (!yang_features_initted) {
	yang_features_initted += 1;
	TAILQ_INIT(&yang_features);
    }

    slaxDataListAdd(&yang_features, feature_name);
}

xmlDocPtr
yangFeaturesBuildInputDoc (void)
{
    xmlDocPtr docp;
    xmlNodePtr top, nodep;
    slax_data_node_t *dnp;

    docp = xmlNewDoc((const xmlChar *) XML_DEFAULT_VERSION);
    if (docp == NULL)
	return NULL;

    docp->standalone = 1;
    docp->dict = xmlDictCreate();

    top = xmlNewDocNode(docp, NULL, (const xmlChar *) "features", NULL);
    if (top == NULL) {
	xmlFreeDoc(docp);
	return NULL;
    }

    xmlDocSetRootElement(docp, top);

    if (yang_features_initted) {
	SLAXDATALIST_FOREACH(dnp, &yang_features) {
	    char *name = dnp->dn_data;

	    /*
	     * The feature is either a simple name or a "name=value"
	     * format.  If there's an equal sign, break it into the
	     * two pieces and put the value as the content of the node.
	     */
	    const char *cp = strchr(name, '=');
	    if (cp) {
		size_t len = cp - name;
		char *newp = alloca(len + 1);
		memcpy(newp, name, len);
		newp[len] = '\0';

		name = newp;
		cp += 1;
	    }

	    nodep = xmlNewDocNode(docp, NULL, (const xmlChar *) name,
				  (const xmlChar *) cp);
	    if (nodep == NULL)
		break;

	    xmlAddChild(top, nodep);
	}
    }

    return docp;
}

#if 0
static xmlNodePtr
yangFindMain (yang_file_t *yfp)
{
    static const char *sel = "xsl:template[@match = '/features']"
	"/node()[local-name() == 'module' || local-name() == 'submodule']";
    xmlNodePtr nodep = NULL;
    
    xmlNodeSetPtr res = slaxXpathSelect(yfp->yf_docp, yfp->yf_root, sel);
    if (res && res->nodeNr == 1) {
	nodep = res->nodeTab[0];
	xmlXPathFreeNodeSet(res);
    }

    return nodep;
}
#endif

static yang_file_t *
yangFileLoadContents (yang_file_list_t *listp,
		      const char *template, const char *name UNUSED,
		      const char *filename, FILE *file,
		      xmlDictPtr dict, int partial UNUSED)
{
    slax_data_t sd;
    yang_data_t yd;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();
    yang_file_t *yfp;

    if (ctxt == NULL)
	return NULL;

    yfp = xmlMalloc(sizeof(*yfp));
    if (yfp == NULL)
	return NULL;

    bzero(yfp, sizeof(*yfp));
    yfp->yf_name = strdup(name);
    yfp->yf_path = strdup(filename);
    TAILQ_INSERT_TAIL(listp, yfp, yf_link);

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    if (dict) {
	if (ctxt->dict)
	    xmlDictFree(ctxt->dict);

    	ctxt->dict = dict;
 	xmlDictReference(ctxt->dict);
    }

    bzero(&sd, sizeof(sd));
    sd.sd_line = 1;

    /* We want to parse SLAX, either full or partial */
    sd.sd_parse = sd.sd_ttype = M_YANG;
    sd.sd_flags |= SDF_SLSH_COMMENTS;

    strncpy(sd.sd_filename, filename, sizeof(sd.sd_filename));
    sd.sd_file = file;

    sd.sd_ctxt = ctxt;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    if (filename)
	xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);

    sd.sd_docp = slaxBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    yfp->yf_docp = sd.sd_docp;
    yfp->yf_root = xmlDocGetRootElement(sd.sd_docp);
#if 0
    yfp->yf_context = xmlXPathNewContext(sd.sd_docp);
#endif

    if (filename != NULL)
        sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) filename);

    /* Add the YIN namespace to the root node */
    xmlNsPtr nsp = xmlNewNs(sd.sd_ctxt->node, (const xmlChar *) YIN_URI,
			    (const xmlChar *) YIN_PREFIX);

    if (slaxElementPush(&sd, ELT_TEMPLATE, NULL, NULL)) {
	if (template) {
	    const char *cp = strrchr(template, '/');
	    slaxAttribAddLiteral(&sd, ATT_NAME, cp ? cp + 1 : template);
	} else {
	    slaxAttribAddLiteral(&sd, ATT_MATCH, "/features");
	}
    }

    bzero(&yd, sizeof(yd));
    sd.sd_opaque = &yd;		/* Hang our data off the slax parser */
    yd.yd_nsp = nsp;
    yd.yd_filep = yfp;
    yd.yd_file_list = listp;

    rc = yangParse(&sd);

    if (yfp->yf_main == NULL) {
	slaxError("%s: no module or submodule found", sd.sd_filename);
	sd.sd_errors += 1;
    }

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing (%d)",
	  sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    return yfp;
}    

void
yangError (slax_data_t *sdp, const char *fmt, ...)
{
    va_list vap;
    char *cp;

    va_start(vap, fmt);
    vasprintf(&cp, fmt, vap);
    slaxError("%s:%d: %s", sdp->sd_filename, sdp->sd_line, cp);
    sdp->sd_errors += 1;

    free(cp);
    va_end(vap);
}

static yang_file_t *
yangFileFind (yang_file_list_t *listp, const char *name)
{
    yang_file_t *yfp;

    TAILQ_FOREACH(yfp, listp, yf_link) {
	if (streq(name, yfp->yf_name))
	    return yfp;
    }

    return NULL;
}

static FILE *
yangFindIncludeFile (const char *name, char *buf, int bufsiz)
{
    static char yang_ext[] = ".yang";
    int len = strlen(name);
    char filename[len + sizeof(yang_ext)];

    memcpy(filename, name, len);
    memcpy(filename + len, yang_ext, sizeof(yang_ext));

    return slaxFindIncludeFile(filename, buf, bufsiz);
}

static void
yangFileFree (yang_file_t *yfp, int free_doc)
{
    xmlFreeAndEasy(yfp->yf_name);
    xmlFreeAndEasy(yfp->yf_path);

    if (free_doc && yfp->yf_docp)
	xmlFreeDoc(yfp->yf_docp);

    if (yfp->yf_context)
	xmlXPathFreeContext(yfp->yf_context);

    xmlFree(yfp);
}

static yang_file_t *
yangFileParse (yang_file_list_t *listp, const char *template,
	       const char *name, const char *filename, FILE *sourcefile,
	       xmlDictPtr dict, int partial)
{
    yang_file_t *yfp;

    yfp = yangFileFind(listp, name);
    if (yfp)
	return yfp;

    yfp = yangFileLoadContents(listp, template, name, filename,
				sourcefile, dict, partial);

    fclose(sourcefile);

#if 0
    yfp->yf_main = yangFindMain(yfp);
    if (yfp->yf_main == NULL) {
	slaxError("%s: could not file main template", filename);
	yangFileFree(yfp, TRUE);
	return NULL;
    }

    uri = yfp->yf_namespace = slaxGetAttrib(yfp->yf_main, YS_NAMESPACE);
    pref = yfp->yf_prefix = slaxGetAttrib(yfp->yf_main, YS_PREFIX);

    if (uri && pref)
	xmlNewNs(yfp->yf_root, (const xmlChar *) uri, (const xmlChar *) pref);
#endif

    return yfp;
}

yang_file_t *
yangFileLoader (const char *template, const char *name,
		const char *filename, xmlDictPtr dict, int partial)
{
    char path[MAXPATHLEN];
    FILE *sourcefile;
    yang_file_list_t list;

    TAILQ_INIT(&list);
    sourcefile = yangFindIncludeFile(name, path, sizeof(path));
    if (sourcefile == NULL)
	return NULL;

    return yangFileParse(&list, template, name, filename, sourcefile,
			 dict, partial);
}

static void
yangImportFile (yang_file_list_t *listp UNUSED, xmlNodePtr insp UNUSED,
		 const char *fname, const char *pref,
		 const char *rev, int is_import)
{
    slaxLog("yang: import: '%s' '%s' '%s' %s",
	    fname ?: "", pref ?: "", rev ?: "", is_import ? " is-import" : "");
}

static const char *
yangGetValue (xmlNodePtr nodep, const char *elt_name, const char *attr_name)
{
    if (nodep == NULL)
	return NULL;

    for (nodep = nodep->children; nodep; nodep = nodep->next) {
	if (nodep->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) nodep->name, elt_name))
	    return slaxGetAttrib(nodep, attr_name);
    }

    return NULL;
}

/*
 * Find and load all imported modules, from which we extract all
 * groupings, typedefs, extensions, features, and identities.  We
 * also handle includes.n
 */
static void
yangHandleImports (yang_file_list_t *listp UNUSED, yang_file_t *filep UNUSED)
{
    xmlNodePtr insp, mainp, nodep, nextp;
    int is_import;

    mainp = filep->yf_main;	/* Look at the current module */
    insp = mainp->parent->parent->children; /* Insertion point */

    for (nodep = mainp->children; nodep; nodep = nextp) {
	nextp = nodep->next;

	if (nodep->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) nodep->name, YS_IMPORT))
	    is_import = TRUE;
	    
	else if (streq((const char *) nodep->name, YS_INCLUDE))
	    is_import = FALSE;

	else
	    continue;

	const char *fname = slaxGetAttrib(nodep, YS_MODULE);
	const char *pref
	    = is_import ? yangGetValue(nodep, YS_PREFIX, YS_VALUE): NULL;
	const char *rev = yangGetValue(nodep, YS_REVISION_DATE, YS_DATE);

	yangImportFile(listp, insp, fname, pref, rev, is_import);
    }
}

/*
 * Find all {,sub}module parameters and move them to be globals.
 * XXX Duplicates should be ignored.
 */
static void
yangHandleGlobals (yang_file_list_t *listp UNUSED, yang_file_t *filep)
{
    xmlNodePtr insp, mainp, nodep, nextp;

    mainp = filep->yf_main;	/* Look at the current module */
    insp = mainp->parent->parent->children; /* Insertion point */

    for (nodep = mainp->children; nodep; nodep = nextp) {
	nextp = nodep->next;

	if (nodep->type != XML_ELEMENT_NODE)
	    continue;

	if (streq((const char *) nodep->name, ELT_PARAM)
	    || streq((const char *) nodep->name, ELT_TEMPLATE)) {
	    slaxLog("moving global '%s' (%p)",
		    (const char *) nodep->name, nodep);
	} else {
	    continue;
	}

	xmlUnlinkNode(nodep);
	xmlAddPrevSibling(insp, nodep);
    }
}

xmlDocPtr
yangLoadFile (const char *template, const char *filename, FILE *file,
	      xmlDictPtr dict, int partial UNUSED)
{
    int len = strlen(filename) + 1;
    char name[len], *cp, *sp;
    yang_file_list_t list;
    yang_file_t *yfp;

    TAILQ_INIT(&list);

    memcpy(name, filename, len);
    sp = strrchr(name, '/');
    cp = strrchr(name, '.');
    if (cp && (sp == NULL || cp > sp))
	*cp = '\0';

    yfp = yangFileParse(&list, template, name, filename, file, dict, partial);
    if (yfp == NULL)
	return NULL;

    xmlDocPtr docp = yfp->yf_docp;
    if (docp) {
	yangHandleImports(&list, yfp);
	yangHandleGlobals(&list, yfp);

	slaxDynLoad(yfp->yf_docp); /* Check dynamic extensions */
    }

    yang_file_t *xp;
    for (;;) {
        xp = TAILQ_FIRST(&list);
        if (xp == NULL)
            break;
        TAILQ_REMOVE(&list, xp, yf_link);
	yangFileFree(xp, (xp != yfp));
    }

    return docp;
}

xmlDocPtr
yangLoadParams (const char *filename, FILE *file,
		xmlDictPtr dict)
{
    slax_data_t sd;
    yang_data_t yd;
    int rc;
    xmlParserCtxtPtr ctxt = xmlNewParserCtxt();

    if (ctxt == NULL)
	return NULL;

    /*
     * Turn on line number recording in each node
     */
    ctxt->linenumbers = 1;

    if (dict) {
	if (ctxt->dict)
	    xmlDictFree(ctxt->dict);

    	ctxt->dict = dict;
 	xmlDictReference(ctxt->dict);
    }

    bzero(&sd, sizeof(sd));
    sd.sd_line = 1;

    /* We want to parse SLAX, either full or partial */
    sd.sd_parse = sd.sd_ttype = M_YANG;
    sd.sd_flags |= SDF_SLSH_COMMENTS;

    strncpy(sd.sd_filename, filename, sizeof(sd.sd_filename));
    sd.sd_file = file;

    sd.sd_ctxt = ctxt;

    ctxt->version = xmlCharStrdup(XML_DEFAULT_VERSION);
    ctxt->userData = &sd;

    /*
     * Fake up an inputStream so the error mechanisms will work
     */
    if (filename)
	xmlSetupParserForBuffer(ctxt, (const xmlChar *) "", filename);

    sd.sd_docp = slaxBuildDoc(&sd, ctxt);
    if (sd.sd_docp == NULL) {
	slaxDataCleanup(&sd);
	return NULL;
    }

    if (filename != NULL)
        sd.sd_docp->URL = (xmlChar *) xmlStrdup((const xmlChar *) filename);

    /* Add the YIN namespace to the root node */
    xmlNewNs(sd.sd_ctxt->node, (const xmlChar *) YIN_URI,
			    (const xmlChar *) YIN_PREFIX);

    bzero(&yd, sizeof(yd));
    sd.sd_opaque = &yd;		/* Hang our data off the slax parser */

    rc = yangParse(&sd);

    if (sd.sd_errors) {
	slaxError("%s: %d error%s detected during parsing (%d)",
	  sd.sd_filename, sd.sd_errors, (sd.sd_errors == 1) ? "" : "s", rc);

	slaxDataCleanup(&sd);
	return NULL;
    }

    /* Save docp before slaxDataCleanup nukes it */
    xmlDocPtr docp = sd.sd_docp;
    sd.sd_docp = NULL;
    slaxDataCleanup(&sd);

    return docp;
}    
