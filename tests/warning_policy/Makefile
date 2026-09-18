TOPSRC!=cd ../..; pwd
PYTHON?=python3

check-host:
	${PYTHON} check.py host --make "${MAKE}"

check-cross:
	${PYTHON} check.py cross --make "${MAKE}"

.PHONY: check-host check-cross
