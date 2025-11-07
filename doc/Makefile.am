DOXYGEN ?= doxygen

if HAVE_DOXYGEN
doc:
	$(AM_V_GEN)$(DOXYGEN) Doxyfile
else
doc:
	@echo "Doxygen is not available; reinstall with --with-doxygen or ensure it is on PATH."
endif

clean-local:
	-rm -rf html latex xml man

.PHONY: doc clean-local
