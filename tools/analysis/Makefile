.if !defined(PYTHON) || empty(PYTHON)
.error set PYTHON to the intended interpreter
.endif

SCRIPTS!=	ls *.py

check:
		for script in ${SCRIPTS}; do \
			${PYTHON} -m py_compile $$script || exit 1; \
		done
		${PYTHON} -B -m unittest -v test_bsd211_patch_scope.py
		${PYTHON} -B -m unittest -v test_bsd211_semantic_ledger.py
		${PYTHON} -B -m unittest -v test_sanitize_bsd211_execution_report.py
		${PYTHON} bsd211_semantic_ledger.py

.PHONY: check
