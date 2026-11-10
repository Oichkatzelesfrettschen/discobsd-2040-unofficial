TOPSRC!=cd ../..; pwd
include ${TOPSRC}/share/mk/sys.mk

# Strict C17 translation of the public headers for the selected target.  -I
# rather than -isystem keeps the headers themselves under -pedantic-errors.
TARGET_CC=	${GCCPREFIX}-gcc
TARGET_FLAGS=	-std=c17 -pedantic-errors -Wall -Wextra -Werror \
		-Wstrict-prototypes -Wmissing-prototypes \
		-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -Os \
		-nostdinc -I${TOPSRC}/include
MUTATE=		${PYTHON} ${.CURDIR}/mutate.py

HEADER_CONTRACTS=	stddef_contract stdalign_contract
HEADER_MUTANTS=		stddef_no_max_align stdalign_weak_alignas

# Each mutant header tree is the public one with a single header replaced,
# found first on the include path.  A contract that still translates against
# it measures nothing.  Both doubleword members go, since either alone
# keeps the union at the greatest alignment.
stddef_no_max_align.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stddef.h $@/stddef.step \
	    "long long	__max_align_integer;" ""
	${MUTATE} $@/stddef.step $@/stddef.h \
	    "long double	__max_align_floating;" ""
stddef_no_max_align.contract= stddef_contract

# alignas that expands to nothing still parses, so only the layout
# assertions see it.
stdalign_weak_alignas.mutant:
	@rm -rf $@ && mkdir $@
	${MUTATE} ${TOPSRC}/include/stdalign.h $@/stdalign.h \
	    "#define alignas			_Alignas" "#define alignas(x)"
stdalign_weak_alignas.contract= stdalign_contract

check-cross: check-header-contracts check-header-mutants

check-header-contracts:
.for contract in ${HEADER_CONTRACTS}
	${TARGET_CC} ${TARGET_FLAGS} -fsyntax-only ${contract}.c
.endfor
	@echo "c17 headers: ${HEADER_CONTRACTS} translate as strict C17"

check-header-mutants: ${HEADER_MUTANTS:S/$/.mutant/}
.for mutant in ${HEADER_MUTANTS}
	@if ${TARGET_CC} -I${mutant}.mutant ${TARGET_FLAGS} -fsyntax-only \
	    ${${mutant}.contract}.c >${mutant}.log 2>&1; then \
		echo "c17 headers: mutant ${mutant} translated" >&2; exit 1; \
	fi
	@grep -q 'error' ${mutant}.log || { cat ${mutant}.log >&2; exit 1; }
.endfor
	@echo "c17 headers: ${HEADER_MUTANTS} rejected"

clean:
	rm -rf *.mutant *.log *.o

.PHONY: check-cross check-header-contracts check-header-mutants clean \
	${HEADER_MUTANTS:S/$/.mutant/}
