PYTHON?=	python3

SCRIPTS!=	ls *.py

check:
		for script in ${SCRIPTS}; do \
			${PYTHON} -m py_compile $$script || exit 1; \
		done

.PHONY: check
