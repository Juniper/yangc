#
# $Id$
#
# Copyright 2011-2014, Juniper Networks, Inc.
# All rights reserved.
# This SOFTWARE is licensed under the LICENSE provided in the
# ../Copyright file. By downloading, installing, copying, or otherwise
# using the SOFTWARE, you agree to be bound by the terms of that
# LICENSE.

TEST_CASES := $(shell cd ${srcdir} ; echo test*.yang )

EXTRA_DIST = \
    ${TEST_CASES} \
    ${addprefix saved/, ${TEST_CASES:.yang=.comp}} \
    ${addprefix saved/, ${TEST_CASES:.yang=.eval}} \
    ${addprefix saved/, ${TEST_CASES:.yang=.err}} \
    ${addprefix saved/, ${TEST_CASES:.yang=.out}}

YANGC=${top_builddir}/yangc/yangc
S2O = | ${SED} '1,/@@/d'
SPDEBUG=
COMP = ${CHECKER} ${YANGC} ${SPDEBUG} -c
EVAL = ${CHECKER} ${YANGC} ${SPDEBUG} -e

CLEANDIRS = out

all:

${YANGC}:
	@(cd ${top_builddir}/yangc ; ${MAKE} yangc)

valgrind:
	@echo '## Running the regression tests under Valgrind'
	${MAKE} CHECKER='valgrind -q' tests

#TEST_TRACE = set -x ; 

TEST_ONE = \
 base=`${BASENAME} $$test .yang` ; \
 ${COMP} ${srcdir}/$$test > out/$$base.comp ; \
 ${DIFF} -Nbu ${srcdir}/saved/$$base.comp out/$$base.comp ${S2O} ; \
 ${EVAL} ${srcdir}/$$test > out/$$base.eval \
   > out/$$base.out 2> out/$$base.err ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.out out/$$base.out ${S2O} ; \
 ${DIFF} -Nu ${srcdir}/saved/$$base.err out/$$base.err ${S2O}

test tests: ${YANGC}
	@${MKDIR} -p out
	-@(for test in ${TEST_CASES} ; do \
	      echo "... $$test ..."; \
	      ${TEST_ONE}; \
              true; \
	done)

one:
	-@(test=${TEST_CASE}; data=${TEST_DATA}; ${TEST_ONE} ; true)

accept:
	-@(for test in ${TEST_CASES} ; do \
	      echo "... $$test ..."; \
	      base=`${BASENAME} $$test .yang` ; \
	      ${CP} out/$$base.comp ${srcdir}/saved/$$base.comp ; \
	      ${CP} out/$$base.eval ${srcdir}/saved/$$base.eval ; \
	      ${CP} out/$$base.out ${srcdir}/saved/$$base.out ; \
	      ${CP} out/$$base.err ${srcdir}/saved/$$base.err ; \
	done)
