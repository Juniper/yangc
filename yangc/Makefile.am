#
# $Id$
#
# Copyright 2011, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

if YANGC_WARNINGS_HIGH
YANGC_WARNINGS = HIGH
endif
include ${top_srcdir}/warnings.mk

AM_CFLAGS = \
    -DLIBSLAX_XMLSOFT_NEED_PRIVATE \
    -I${top_srcdir} \
    -I${top_srcdir}/libyang \
    -I${top_builddir} \
    ${LIBSLAX_CFLAGS} \
    ${LIBXSLT_CFLAGS} \
    ${LIBXML_CFLAGS} \
    ${WARNINGS}

LIBS = \
    ${LIBSLAX_LIBS} \
    ${LIBXSLT_LIBS} \
    -lexslt \
    ${LIBXML_LIBS}

#noinst_HEADERS = \
#    yangc.h

if YANGC_DEBUG
AM_CFLAGS += -g -DYANGC_DEBUG
endif

AM_CFLAGS += \
 -DYANGC_DIR=\"${YANGC_DIR}\"

if HAVE_READLINE
LIBS += -L/opt/local/lib -lreadline
endif

if HAVE_LIBEDIT
LIBS += -ledit
endif

if HAVE_LIBM
LIBS += -lm
endif

bin_PROGRAMS = yangc

yangc_SOURCES = yangc.c
yangc_LDADD = ../libyang/libyang.la
yangc_LDFLAGS = -static

man_MANS = yangc.1x

EXTRA_DIST = yangc.1x
