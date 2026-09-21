TOPSRC!=cd ../..; pwd

include ${TOPSRC}/share/mk/architecture.mk

.if ${BUILD_LEGACY_NON_ARM} != "yes"
.error legacy/non-arm requires BUILD_LEGACY_NON_ARM=yes
.endif

MAP=	${TOPSRC}/tools/architecture-isolation/legacy-path-map.tsv
VERIFY=	${TOPSRC}/tools/architecture-isolation/verify-legacy-path-map.sh

all install check:
	sh ${VERIFY} ${TOPSRC} ${MAP} archive
	@echo "legacy non-ARM sources are archived and have no supported build"

clean:
	@:

.PHONY: all install check clean
