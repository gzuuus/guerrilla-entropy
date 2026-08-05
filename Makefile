# Release helper. Bump version, tag, and push — triggers .github/workflows/release.yml.
# Derives the next version from the latest git tag (v0.0.0 if none).
#   make patch | minor | major
SHELL := /bin/bash

.PHONY: patch minor major

patch minor major:
	@cur=$$(git describe --tags --abbrev=0 2>/dev/null || echo v0.0.0); \
	IFS=. read -r maj min pat <<< "$${cur#v}"; \
	case "$@" in \
	  patch) pat=$$((pat+1));; \
	  minor) min=$$((min+1)); pat=0;; \
	  major) maj=$$((maj+1)); min=0; pat=0;; \
	esac; \
	new="v$$maj.$$min.$$pat"; \
	echo "Bump $$cur -> $$new"; \
	git tag -a "$$new" -m "Release $$new" && \
	git push origin "$$new" && \
	echo "Pushed $$new — release.yml will build and publish guerrilla-entropy-merged.bin"
