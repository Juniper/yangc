#
# $Id$
#
# Copyright 2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if YANGC_WARNINGS_HIGH
YANGC_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

yangincdir = ${includedir}/libyang

BISON_COMMAND = ${BISON} -kvd -b slaxparser
ECHO_BISON_0 = @echo "  BISON " $@;
ECHO_BISON_ = ${ECHO_BISON_${AM_DEFAULT_VERBOSITY}}
ECHO_BUILD_0 = @echo "  BUILD " $@;
ECHO_BUILD_ = ${ECHO_BUILD_${AM_DEFAULT_VERBOSITY}}

AUTOMAKE_OPTIONS = subdir-objects

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -DYANGC_LIBEXECDIR=\"${YANGC_LIBEXECDIR}\" \
    -I${top_srcdir} \
    -I${top_builddir} \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${WARNINGS} 

lib_LTLIBRARIES = libyang.la

yanginc_HEADERS = 

noinst_HEADERS = \
    yangconfig.h

YANGHEADERS = ${noinst_HEADERS} yangparser.h

libyang_la_SOURCES = \
    yangbuiltin.c \
    yangloader.c \
    yangstmt.c \
    yangparser.c \
    yangwriter.c

LIBS = \
    ${LIBSLAX_LIBS} \
    ${LIBXSLT_LIBS} \
    ${LIBXML_LIBS}

SED_ARGS = \
-e 's:int yyparse (void);::' \
-e 's/yyparse (void[^;]*/yangParse (slax_data_t *slax_data)/' \
-e 's:yyvs = yyvsa;:yyvs = yyvsa; bzero(yyvsa, sizeof(yyvsa));:'

yangparser.h yangparser.c: yangparser-out.y
	${ECHO_BISON_${V}}${BISON_COMMAND} -o yangparser.c yangparser-out.y
	@${MV} yangparser.c yangparser.c.old
	@${SED} ${SED_ARGS} < yangparser.c.old  > yangparser.c
	@${RM} yangparser.c.old

CLEANFILES = yangparser.h yangparser.c yangparser.output

#
# See the comment at the top of slaxparser-xp.y for details
#
YANGPIECES = \
    ${srcdir}/yangparser.y \
    ${LIBSLAX_INTERNALDIR}/slaxparser-xp.y \
    slaxparser-xpl.y \
    ${LIBSLAX_INTERNALDIR}/slaxparser-tail.y


if HAVE_BISON30
BISON_HEADER = "%param {slax_data_t *slax_data}"
else
BISON_HEADER = 
endif

yangparser-out.y: ${YANGPIECES}
	${ECHO_BUILD_${V}}(echo ${BISON_HEADER} ; cat ${YANGPIECES} ) > $@

yangparser.h: yangparser.c
${libyang_la_SOURCES:.c=.o}: ${YANGHEADERS}
${libyang_la_SOURCES:.c=.lo}: ${YANGHEADERS}

EXTRA_DIST = \
    yangparser.y

CLEANFILES += yangparser-out.y slaxparser-xpl.y

slaxparser-xpl.y: ${LIBSLAX_INTERNALDIR}slaxparser-xp.y
	${ECHO_BUILD_${V}}sed 's/xp_/xpl_/g' \
		< ${LIBSLAX_INTERNALDIR}/slaxparser-xp.y > slaxparser-xpl.y

clean-yangparser:
	${RM} -f slaxparser-xpl.y yangparser-out.y

svnignore:
	svn propset svn:ignore -F ${srcdir}/.svnignore ${srcdir}

CLEANFILES += yangconfig.h yangparser.d 
