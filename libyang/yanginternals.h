/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

#ifndef YANG_INTERNALS_H
#define YANG_INTERNALS_H

#include "slaxinternals.h"

/* In case these are already seen (from slaxconfig.h) */
#undef PACKAGE
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef VERSION

#include "yangconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>

#include <libxslt/documents.h>

extern int yangYyDebug;		/* yydebug from yangparser.c */

#endif /* YANG_INTERNALS_H */
