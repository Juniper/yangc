/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * See ../Copyright for the status of this software
 */

struct yang_stmt_s;
struct yang_data_s;

#define YANG_STMT_OPEN_ARGS \
    slax_data_t *sdp UNUSED, struct yang_data_s *ydp UNUSED, \
	struct yang_stmt_s *ysp UNUSED
#define YANG_STMT_CLOSE_ARGS \
    slax_data_t *sdp UNUSED, struct yang_data_s *ydp UNUSED, \
	struct yang_stmt_s *ysp UNUSED
#define YANG_STMT_SETARG_ARGS \
    slax_data_t *sdp UNUSED, struct yang_data_s *ydp UNUSED, \
	struct yang_stmt_s *ysp UNUSED

typedef struct yang_relative_s {
    const char *yr_name;	/* Name of our relative */
    const char *yr_namespace;	/* Namespace (NULL means our's) */
    unsigned yr_flags;		/* Flags for this relative */
} yang_relative_t;

/* Flags for yr_flags */
#define YRF_MULTIPLE	(1<<0)	/* Allow multiple occurances (0..n)*/
#define YRF_MANDATORY	(1<<1)	/* Mandatory (1..n or 1) */

typedef struct yang_stmt_s {
    TAILQ_ENTRY(yang_stmt_s) ys_link; /* Next statement */
    unsigned ys_id; /* Identifier for this statement */
    const char *ys_name;	/* The name of this statement */
    const char *ys_namespace;   /* XML namespace */
    const char *ys_argument;	/* YIN attribute name for argument */
    unsigned ys_flags;		/* Flags for this statement (YSF_*) */
    unsigned ys_type;		/* Type of argument (Y_*) */
    yang_relative_t *ys_parents; /* Array of acceptable parent statements */
    yang_relative_t *ys_children; /* Array of acceptable children statements */
    int (*ys_open)(YANG_STMT_OPEN_ARGS); /* Statement is opened */
    int (*ys_close)(YANG_STMT_CLOSE_ARGS); /* Statement is closed */
    int (*ys_setarg)(YANG_STMT_SETARG_ARGS); /* Argument is set */
} yang_stmt_t;

/* Flags for yang_stmt_t: */
#define YSF_YINELEMENT	(1<<0)	/* Encode as an element in YIN */
#define YSF_STANDARD	(1<<1)	/* Statement is YANG state */
#define YSF_CHILDREN_ALLOCED (1<<2) /* ys_children was malloced, needs freed */

#define YS_MULTIPLE	"*"	/* Allow multiple instances */
#define YS_MULTIPLE_CHAR '*'	/* Ditto (as character) */

typedef TAILQ_HEAD(yang_stmt_list_s, yang_stmt_s) yang_stmt_list_t;

void
yangStmtInit (void);

void
yangStmtAdd (yang_stmt_t *ysp, const char *namespace, int count);

yang_stmt_t *
yangStmtFind (const char *namespace, const char *name);

void
yangStmtOpen (slax_data_t *sdp, const char *name);

void
yangStmtClose (slax_data_t *sdp, const char *name);

void
yangStmtSetArgument (slax_data_t *sdp, slax_string_t *value, int is_xpath);

slax_string_t *
yangConcatValues (slax_data_t *sdp, slax_string_t *one,
		  slax_string_t *two, int with_space);

#define YS_ANYXML		"anyxml"
#define YS_ARGUMENT		"argument"
#define YS_AUGMENT		"augment"
#define YS_BASE			"base"
#define YS_BELONGS_TO		"belongs-to"
#define YS_BIT			"bit"
#define YS_CASE			"case"
#define YS_CHOICE		"choice"
#define YS_CONDITION		"condition"
#define YS_CONFIG		"config"
#define YS_CONTACT		"contact"
#define YS_CONTAINER		"container"
#define YS_DATE			"date"
#define YS_DEFAULT		"default"
#define YS_DESCRIPTION		"description"
#define YS_DEVIATE		"deviate"
#define YS_DEVIATION		"deviation"
#define YS_ENUM			"enum"
#define YS_ERROR_APP_TAG	"error-app-tag"
#define YS_ERROR_MESSAGE	"error-message"
#define YS_EXTENSION		"extension"
#define YS_FEATURE		"feature"
#define YS_FRACTION_DIGITS	"fraction-digits"
#define YS_GROUPING		"grouping"
#define YS_HELP			"help"
#define YS_IDENTITY		"identity"
#define YS_IF_FEATURE		"if-feature"
#define YS_IMPORT		"import"
#define YS_INCLUDE		"include"
#define YS_INPUT		"input"
#define YS_KEY			"key"
#define YS_LEAF			"leaf"
#define YS_LEAF_LIST		"leaf-list"
#define YS_LENGTH		"length"
#define YS_LIST			"list"
#define YS_MANDATORY		"mandatory"
#define YS_MAX_ELEMENTS		"max-elements"
#define YS_MIN_ELEMENTS		"min-elements"
#define YS_MODULE		"module"
#define YS_MUST			"must"
#define YS_NAME			"name"
#define YS_NAMESPACE		"namespace"
#define YS_NOTIFICATION		"notification"
#define YS_ORDERED_BY		"ordered-by"
#define YS_ORGANIZATION		"organization"
#define YS_OUTPUT		"output"
#define YS_PATH			"path"
#define YS_PATTERN		"pattern"
#define YS_POSITION		"position"
#define YS_PREFIX		"prefix"
#define YS_PRESENCE		"presence"
#define YS_RANGE		"range"
#define YS_REFERENCE		"reference"
#define YS_REFINE		"refine"
#define YS_REQUIRE_INSTANCE	"require-instance"
#define YS_REVISION		"revision"
#define YS_REVISION_DATE	"revision-date"
#define YS_RPC			"rpc"
#define YS_STATUS		"status"
#define YS_SUBMODULE		"submodule"
#define YS_TAG			"tag"
#define YS_TARGET_NODE		"target-node"
#define YS_TEXT			"text"
#define YS_TYPE			"type"
#define YS_TYPEDEF		"typedef"
#define YS_UNIQUE		"unique"
#define YS_UNITS		"units"
#define YS_URI			"uri"
#define YS_USES			"uses"
#define YS_VALUE		"value"
#define YS_WHEN			"when"
#define YS_YANG_VERSION		"yang-version"
#define YS_YIN_ELEMENT		"yin-element"

/* Names for YANGC extensions */
#define YS_CHILDREN		"children"
#define YS_NAMES		"names"
#define YS_PARENTS		"parents"

void yangStmtInitBuiltin (void);

xmlNodePtr
yangStmtGetNodeName (slax_data_t *sdp, xmlNodePtr nodep,
		     const char *namespace, const char *name);

xmlNodePtr
yangStmtGetNode (slax_data_t *sdp, xmlNodePtr nodep, yang_stmt_t *ysp);

char *
yangStmtGetValueName (slax_data_t *sdp, xmlNodePtr nodep,
		      const char *namespace, const char *name,
		      const char *argument, unsigned flags);
char *
yangStmtGetValue (slax_data_t *sdp, xmlNodePtr nodep, yang_stmt_t *ysp);

void
yangStmtCheckArgument (slax_data_t *sdp, slax_string_t *sp);
