/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

#include <err.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <pwd.h>
#include <sys/socket.h>

#include <libxml/tree.h>
#include <libxml/dict.h>
#include <libxml/uri.h>
#include <libxml/xpathInternals.h>
#include <libxml/parserInternals.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxslt/transform.h>
#include <libxml/HTMLparser.h>
#include <libxml/xmlsave.h>
#include <libexslt/exslt.h>
#include <libxslt/xsltutils.h>

#include <libslax/slaxconfig.h>
#include <libslax/slax.h>
#include <libslax/slaxdata.h>
#include <libslax/xmlsoft.h>

#include "yanginternals.h"
#include <libyang/yang.h>
#include <libyang/yangversion.h>
#include <libyang/yangloader.h>
#include <libyang/yangstmt.h>

static slax_data_list_t plist;
static int nbparams;
static slax_data_list_t param_files;

static int options = XSLT_PARSE_OPTIONS;

static char *encoding;		/* Desired document encoding */

static int opt_indent = TRUE;	/* Indent the output (pretty print) */
static int opt_partial;		/* Parse partial contents */
static int opt_debugger;	/* Invoke the debugger */

/*
 * Shamelessly lifted from slaxproc.c
 */
static const char *
get_filename (const char *filename, char ***pargv, int outp)
{
    if (filename == NULL) {
        filename = **pargv;
        if (filename)
            *pargv += 1;
        else filename = "-";
    }

    if (outp >= 0 && slaxFilenameIsStd(filename))
        filename = outp ? "/dev/stdout" : "/dev/stdin";
    return filename;
}

static void
mergeParamFile (xmlDocPtr docp UNUSED, xmlDocPtr sourcedoc UNUSED)
{
    slaxLog("handleParams: %p %p", docp, sourcedoc);

    xmlNodePtr insp = sourcedoc->children->children;
    xmlNodePtr nodep, newp;

    for (nodep = docp->children->children; nodep; nodep = nodep->next) {
	newp = xmlDocCopyNode(nodep, sourcedoc, 1);
	xmlAddPrevSibling(insp, newp);
    }
}

static int
do_eval (xmlDocPtr sourcedoc, const char *sourcename, const char *input)
{
    xmlDocPtr indoc;
    xmlDocPtr res = NULL;
    xsltStylesheetPtr source;
    const char **params = alloca(nbparams * 2 * sizeof(*params) + 1);
    slax_data_node_t *dnp;
    int i = 0;

    SLAXDATALIST_FOREACH(dnp, &plist) {
	params[i++] = dnp->dn_data;
    }

    params[i] = NULL;

    source = xsltParseStylesheetDoc(sourcedoc);
    if (source == NULL || source->errors != 0)
	errx(1, "%d errors parsing source: '%s'",
	     source ? source->errors : 1, sourcename);

    SLAXDATALIST_FOREACH(dnp, &param_files) {
	const char *name = dnp->dn_data;
	FILE *fp = fopen(name, "r");
	if (fp == NULL)
	    err(1, "cannot open parameter file '%s'", name);

	xmlDocPtr docp = yangLoadParams(name, fp, NULL);
	if (docp) {
	    mergeParamFile(docp, sourcedoc);
	    xmlFreeDoc(docp);
	}

	fclose(fp);
    }

    if (input)
	indoc = xmlReadFile(input, encoding, options);
    else
	indoc = yangFeaturesBuildInputDoc();

    if (indoc == NULL)
	errx(1, "unable to parse: '%s'", input);

    if (opt_indent)
	source->indent = 1;

    if (opt_debugger) {
	slaxDebugInit();
	slaxDebugSetStylesheet(source);
	res = slaxDebugApplyStylesheet(sourcename, source,
				 slaxFilenameIsStd(input) ? NULL : input,
				 indoc, params);
    } else {
	res = xsltApplyStylesheet(source, indoc, params);
    }

    if (res) {
	FILE *outfile = stdout;

	xsltSaveResultToFile(outfile, res, source);
	yangWriteDoc((slaxWriterFunc_t) fprintf, outfile, res, 0);

	xmlFreeDoc(res);
    }

    xmlFreeDoc(indoc);
    xsltFreeStylesheet(source);

    return 0;
}

static int
do_post (const char *name, const char *output UNUSED,
	 const char *input, char **argv)
{
    name = get_filename(name, &argv, -1);
    
    xmlDocPtr docp = xmlReadFile(name, NULL, XSLT_PARSE_OPTIONS);
    if (docp == NULL) {
	errx(1, "cannot parse file: '%s'", name);
        return -1;
    }

    return do_eval(docp, name, input);
}

static int
do_work (const char *name, const char *output, const char *input,
	 char **argv, int full_eval)
{
    xmlDocPtr sourcedoc;
    const char *sourcename;
    FILE *sourcefile, *outfile;
    char buf[BUFSIZ];

    sourcename = get_filename(name, &argv, -1);
    output = get_filename(output, &argv, -1);

    if (slaxFilenameIsStd(sourcename))
	errx(1, "source file cannot be stdin");

    sourcefile = slaxFindIncludeFile(sourcename, buf, sizeof(buf));
    if (sourcefile == NULL)
	err(1, "file open failed for '%s'", sourcename);

    sourcedoc = yangLoadFile(NULL, sourcename, sourcefile, NULL, 0);
    if (sourcedoc == NULL)
	errx(1, "cannot parse: '%s'", sourcename);
    if (sourcefile != stdin)
	fclose(sourcefile);

    if (output == NULL || slaxFilenameIsStd(output))
	outfile = stdout;
    else {
	outfile = fopen(output, "w");
	if (outfile == NULL)
	    err(1, "could not open output file: '%s'", output);
    }

    if (full_eval) {
	return do_eval(sourcedoc, sourcename, input);
    } else {
	slaxDumpToFd(fileno(outfile), sourcedoc, FALSE);
    }

    return 0;
}

static int
do_compile (const char *name, const char *output,
	    const char *input, char **argv)
{
    return do_work(name, output, input, argv, FALSE);
}

static int
do_evaluate (const char *name, const char *output,
	     const char *input, char **argv)
{
    return do_work(name, output, input, argv, TRUE);
}

static void
print_version (void)
{
    printf("libyang version %s%s\n",  YANGC_VERSION, YANGC_VERSION_EXTRA);
    printf("libslax version %s%s\n",  LIBSLAX_VERSION, LIBSLAX_VERSION_EXTRA);
    printf("Using libxml %s, libxslt %s and libexslt %s\n",
	   xmlParserVersion, xsltEngineVersion, exsltLibraryVersion);
    printf("yangc was compiled against libxml %d%s, "
	   "libxslt %d%s and libexslt %d\n",
	   LIBXML_VERSION, LIBXML_VERSION_EXTRA,
	   LIBXSLT_VERSION, LIBXSLT_VERSION_EXTRA, LIBEXSLT_VERSION);
    printf("libxslt %d was compiled against libxml %d\n",
	   xsltLibxsltVersion, xsltLibxmlVersion);
    printf("libexslt %d was compiled against libxml %d\n",
	   exsltLibexsltVersion, exsltLibxmlVersion);
}

static void
print_help (void)
{
    fprintf(stderr, "Usage: yangc [options] [files]\n\n");
}

int
main (int argc UNUSED, char **argv)
{
    char *cp;
    const char *input = NULL, *output = NULL, *name = NULL, *trace_file = NULL;
    int (*func)(const char *, const char *, const char *, char **) = NULL;
    FILE *trace_fp = NULL;
    int randomize = 1;
    int logger = FALSE;
    int use_exslt = TRUE;
    char *opt_log_file = NULL;

    setenv("MallocScribble", "true", 1);

    slaxDataListInit(&plist);
    slaxDataListInit(&param_files);

    for (argv++; *argv; argv++) {
	cp = *argv;

	if (*cp != '-')
	    break;

	if (streq(cp, "--compile") || streq(cp, "-c")) {
	    if (func)
		errx(1, "open one action allowed");
	    func = do_compile;

	} else if (streq(cp, "--debug") || streq(cp, "-d")) {
	    opt_debugger = TRUE;

	} else if (streq(cp, "--evaluate") || streq(cp, "-e")) {
	    func = do_evaluate;

	} else if (streq(cp, "--feature") || streq(cp, "-f")) {
	    yangFeatureAdd(*++argv);

	} else if (streq(cp, "--help") || streq(cp, "-h")) {
	    print_help();
	    return -1;

	} else if (streq(cp, "--include") || streq(cp, "-I")) {
	    slaxIncludeAdd(*++argv);

	} else if (streq(cp, "--input") || streq(cp, "-i")) {
	    input = *++argv;

	} else if (streq(cp, "--log") || streq(cp, "-l")) {
	    opt_log_file = *++argv;

	} else if (streq(cp, "--name") || streq(cp, "-n")) {
	    name = *++argv;

	} else if (streq(cp, "--no-randomize")) {
	    randomize = 0;

	} else if (streq(cp, "--output") || streq(cp, "-o")) {
	    output = *++argv;

	} else if (streq(cp, "--param") || streq(cp, "-a")) {
	    char *pname = *++argv;
	    char *pvalue = *++argv;
	    char *tvalue;
	    char quote;
	    int plen;

	    if (pname == NULL || pvalue == NULL)
		errx(1, "missing parameter value");

	    plen = strlen(pvalue);
	    tvalue = xmlMalloc(plen + 3);
	    if (tvalue == NULL)
		errx(1, "out of memory");

	    quote = strrchr(pvalue, '\"') ? '\'' : '\"';
	    tvalue[0] = quote;
	    memcpy(tvalue + 1, pvalue, plen);
	    tvalue[plen + 1] = quote;
	    tvalue[plen + 2] = '\0';

	    nbparams += 1;
	    slaxDataListAddNul(&plist, pname);
	    slaxDataListAddNul(&plist, tvalue);

	} else if (streq(cp, "--partial")) {
	    opt_partial = TRUE;

	} else if (streq(cp, "--param-file") || streq(cp, "-P")) {
	    slaxDataListAddNul(&param_files, *++argv);

	} else if (streq(cp, "--post") || streq(cp, "-p")) {
	    func = do_post;

	} else if (streq(cp, "--trace") || streq(cp, "-t")) {
	    trace_file = *++argv;

	} else if (streq(cp, "--verbose") || streq(cp, "-v")) {
	    logger = TRUE;

	} else if (streq(cp, "--version") || streq(cp, "-V")) {
	    print_version();
	    exit(0);

	} else if (streq(cp, "--yydebug") || streq(cp, "-y")) {
	    yangYyDebug = TRUE;

	} else {
	    fprintf(stderr, "invalid option: %s\n", cp);
	    print_help();
	    return -1;
	}
    }

    cp = getenv("SLAXPATH");
    if (cp)
	slaxIncludeAddPath(cp);

    /*
     * Seed the random number generator.  This is optional to allow
     * test jigs to take advantage of the default stream of generated
     * numbers.
     */
    if (randomize)
	slaxInitRandomizer();

    /*
     * Start the XML API
     */
    xmlInitParser();
    xsltInit();
    slaxEnable(SLAX_ENABLE);
    slaxIoUseStdio(0);
    yangStmtInit();

    if (opt_log_file) {
	FILE *fp = fopen(opt_log_file, "w");
	if (fp == NULL)
	    err(1, "could not open log file: '%s'", opt_log_file);

	slaxLogEnable(TRUE);
	slaxLogToFile(fp);

    } else if (logger) {
	slaxLogEnable(TRUE);
    }

    if (use_exslt)
	exsltRegisterAll();

    if (trace_file) {
	if (slaxFilenameIsStd(trace_file))
	    trace_fp = stderr;
	else {
	    trace_fp = fopen(trace_file, "w");
	    if (trace_fp == NULL)
		err(1, "could not open trace file: '%s'", trace_file);
	}
	slaxTraceToFile(trace_fp);
    }

    if (func == NULL)
	func = do_compile;

    func(name, output, input, argv);
    
    if (trace_fp && trace_fp != stderr)
	fclose(trace_fp);

    slaxDynClean();
    xsltCleanupGlobals();
    xmlCleanupParser();

    exit(slaxGetExitCode());
}
