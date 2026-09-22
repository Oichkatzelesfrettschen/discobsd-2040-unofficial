# Receipts surround executable recipes; target reachability alone cannot
# establish that a width-specific branch ran. The caller selects PYTHON.
TEST_EXECUTION= ${PYTHON} ${TOPSRC}/tools/test_execution.py
ILP32_OK!= sh ${TOPSRC}/tools/compiler-probe.sh ilp32 ${ILP32_CC} -m32
