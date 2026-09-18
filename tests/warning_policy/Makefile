# Compiler enforcement and failure propagation, using the real make fragments.
PYTHON?= python3

check:
	${PYTHON} check.py --make ${MAKE:Q}

all: check

.PHONY: all check
