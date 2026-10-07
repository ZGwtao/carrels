# SPDX-FileCopyrightText: 2026 UNSW
#
# SPDX-License-Identifier: BSD-2-Clause

REUSE ?= reuse
CLANG_FORMAT ?= clang-format

FORMAT_FILES := $(shell git ls-files \
	'*.c' \
	'*.h' )
LICENSE_C_FILES := $(shell git ls-files \
	'*.c' \
	'*.h' \
	'*.S')
LICENSE_FILES := $(shell git ls-files \
	'*.py' \
	'*.mk' \
	'*.sh' \
	'*.yml' \
	'*.yaml' \
	'Makefile' \
	'README.md' \
	'*.config')

.PHONY: format format-check license-check \
	license-annotate license-annotate-c license-annotate-others

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

license-annotate-c:
	$(REUSE) annotate --style=c --multi-line \
		--copyright="UNSW" --year=2026 \
		--license=BSD-2-Clause \
		$(LICENSE_C_FILES)

license-annotate-others:
	$(REUSE) annotate \
		--copyright="UNSW" --year=2026 \
		--license=BSD-2-Clause --skip-unrecognised \
		$(LICENSE_FILES)

license-annotate: license-annotate-c license-annotate-others

license-check:
	$(REUSE) lint
