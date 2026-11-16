#
# fstat's process-name choice, proc_comm() in ../fstat.c, copied out of the
# production source and tested on the host; see proc_comm_gate.py.
#
#   check   the extraction test and its pid-0 mutant
#
# The caller selects PYTHON, as for every Python-backed gate in the tree.
#
TOPSRC!=cd ../../..; pwd
HOSTCC?=	cc

check:
		HOSTCC=${HOSTCC:Q} ${PYTHON} -I -u proc_comm_gate.py ${TOPSRC}

clean:
		rm -rf __pycache__

.PHONY:		check clean
