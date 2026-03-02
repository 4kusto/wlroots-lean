SHELL := /bin/bash

CN_FILES := Wlroots/c/shim_ffi_cn.c Wlroots/c/shim_wlroots_cn.c
CN_FLAGS := --magic-comment-char-dollar
CN_TEST_OUTPUT_BASE := /tmp/cn-shim-tests
NIX_DEV := nix develop --impure path:. -c
FD_TEST_BIN := /tmp/wlroots-shim-fd-test

.PHONY: lean-build cn-verify cn-test fd-test-build fd-test check

lean-build:
	$(NIX_DEV) lake build

cn-verify:
	@for f in $(CN_FILES); do \
		cn verify $$f $(CN_FLAGS) --quiet; \
	done

cn-test:
	@for f in $(CN_FILES); do \
		out="$(CN_TEST_OUTPUT_BASE)-$$(basename $$f .c)"; \
		rm -rf "$$out"; \
		cn test $$f $(CN_FLAGS) --output-dir "$$out" --num-samples=200; \
	done

fd-test-build:
	$(NIX_DEV) sh -lc '\
		set -eu; \
		if pkg-config --exists wlroots-0.19; then WLR_PKG=wlroots-0.19; else WLR_PKG=wlroots; fi; \
		cc -O2 -std=c11 -DWLR_USE_UNSTABLE -DSHIM_NO_LEAN_FFI \
		  -I Wlroots/c \
		  $$(pkg-config --cflags $$WLR_PKG wayland-server xkbcommon) \
		  Wlroots/c/shim.c Wlroots/c/shim_fd_test.c \
		  -o $(FD_TEST_BIN) \
		  $$(pkg-config --libs $$WLR_PKG wayland-server xkbcommon); \
	'

fd-test: fd-test-build
	$(NIX_DEV) sh -lc 'timeout 8s $(FD_TEST_BIN)'

check: cn-verify cn-test fd-test lean-build
