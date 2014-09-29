/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#ifndef LIBYANG_YANG_H
#define LIBYANG_YANG_H

#define YIN_URI "urn:ietf:params:xml:ns:yang:yin:1"
#define YIN_PREFIX "yin"

#define YANGC_URI "http://juise.org/yangc/1.0"
#define YANGC_PREFIX "yangc"

struct _xmlDoc;
struct _xmlDict;
struct _xmlNode;

struct _xmlDoc *
yangLoadFile (const char *template, const char *filename,
	      FILE *file, struct _xmlDict *dict, int partial);

int
yangWriteDocNode (slaxWriterFunc_t func, void *data,
		  struct _xmlNode *nodep, unsigned flags);

int
yangWriteDoc (slaxWriterFunc_t func, void *data,
	      struct _xmlDoc *docp, unsigned flags);

struct slax_data_list_s;	/* Forward decl */
int
yangEvalDoc (xmlDocPtr sourcedoc, const char *sourcename, const char *input,
	     const char **params, struct slax_data_list_s *param_files,
	     unsigned flags);
/* Flags for yangEvalDoc */
#define YEF_INDENT	(1<<0)	/* Indent output */
#define YEF_DEBUGGER	(1<<1)	/* Invoke debugger (sdb) */

#endif /* LIBYANG_YANG_H */
