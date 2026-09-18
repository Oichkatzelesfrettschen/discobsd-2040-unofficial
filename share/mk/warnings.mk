# Keep fatal diagnostics independent of CFLAGS: leaf makefiles often replace
# optimization flags. Warning groups remain local to each source family.
WARNERR=	-Werror
