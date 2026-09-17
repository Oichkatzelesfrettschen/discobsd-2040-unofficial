RUNNER=         ${.CURDIR}/run.py
PROFILE=        ${.CURDIR}/v7_rl02.simh
EXPECTED=       ${.CURDIR}/v7_rl02.expected
SIMULATOR?=     simh-pdp11
PYTHON?=        python3

check-python:
	@if [ -z "${PYTHON}" ]; then \
		echo "PYTHON must name the interpreter for PDP-11 reference tests" >&2; \
		exit 2; \
	fi

check: check-python
	"${PYTHON}" -B -m unittest -v test_runner.py

reference: check check-image
	"${PYTHON}" -B "${RUNNER}" \
	    --simulator "${SIMULATOR}" \
	    --image "${PDP11_V7_IMAGE}" \
	    --profile "${PROFILE}" \
	    --expected "${EXPECTED}" \
	    ${PDP11_EVIDENCE_DIR:D--evidence-dir "${PDP11_EVIDENCE_DIR}"}

check-image:
	@if [ -z "${PDP11_V7_IMAGE}" ]; then \
		echo "PDP11_V7_IMAGE must name the externally supplied V7 RL02 image" >&2; \
		exit 2; \
	fi

all: check

.PHONY: all check check-image check-python reference
