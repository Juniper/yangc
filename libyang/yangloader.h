/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 */

typedef struct yang_file_s {
    TAILQ_ENTRY(yang_file_s) yf_link; /* Next file */
    char *yf_name;		      /* Name of this module or submodule */
    unsigned yf_flags;		      /* Flags (YFF_*) */
    xmlDocPtr yf_docp;		      /* Parse document */
    xmlNodePtr yf_root;		      /* Root node of doc: xsl:stylesheet */
    xmlNodePtr yf_main;		      /* Main template doc: yin:{,sub}module */
    char *yf_namespace;		      /* XML namespace for this content */
    char *yf_prefix;		      /* XML prefix for this content */
    char *yf_path;		      /* Full path to the file */
    char *yf_revision;		      /* Revision date (or null) */
    xmlXPathContextPtr yf_context;    /* Context for functions/select */
} yang_file_t;

typedef TAILQ_HEAD(yang_file_list_s, yang_file_s) yang_file_list_t;

/* Flags for yf_flags: */
#define YFF_IMPORT	(1<<0)	/* Imported (not included) */
#define YFF_MODULE	(1<<1)	/* File is a module */

#define YANG_MAX_STATEMENT_MAP 256
#ifndef NBBY
#define NBBY 8
#endif /* NBBY */

typedef unsigned char yang_seen_elt_t;
typedef struct yang_seen_s {
    yang_seen_elt_t yss_map[YANG_MAX_STATEMENT_MAP / NBBY];
} yang_seen_t;

typedef struct yang_parse_stack_s {
    struct yang_stmt_s *yps_stmt; /* Our statement */
    unsigned yps_flags;		 /* Flags (YPSF_*) */
    yang_seen_t yps_seen;	 /* Substatements we have seen */
} yang_parse_stack_t;

/* Flags for yps_flags: */
#define YPSF_DISCARD	(1<<0)	/* Discard when closed (an error) */

#define YANG_STACK_MAX_DEPTH 256 /* Max number of nested statements */

typedef struct yang_data_s {
    yang_parse_stack_t yd_stack[YANG_STACK_MAX_DEPTH]; /* Open stmts */
    yang_parse_stack_t *yd_stackp;	/* Pointer into yd_stack */
    xmlNsPtr yd_nsp;		/* Point to the 'yin' namespace */
    yang_file_t *yd_filep;	/* Current file */
    yang_file_list_t *yd_file_list; /* List of current files */
} yang_data_t;

/*
 * Find parent parse stack frame
 */
static inline yang_parse_stack_t *
yangStackParent (yang_data_t *ydp, yang_parse_stack_t *ypsp)
{
    if (ypsp == NULL || ypsp == ydp->yd_stack)
	return NULL;
    return ypsp - 1;
}

/*
 * Retrieve the yang data pointer from a slax data block
 */
static inline yang_data_t *
yangData (slax_data_t *sdp)
{
    return sdp->sd_opaque;
}

/*
 * The bison-based parser's main function
 */
int
yangParse (slax_data_t *);

yang_file_t *
yangFileLoader (const char *template, const char *name,
		const char *filename, xmlDictPtr dict, int partial);

void
yangError (slax_data_t *sdp, const char *fmt, ...);

void
yangFeatureAdd (const char *feature_name);

xmlDocPtr
yangFeaturesBuildInputDoc (void);

xmlDocPtr
yangLoadParams (const char *filename, FILE *file, xmlDictPtr dict);
