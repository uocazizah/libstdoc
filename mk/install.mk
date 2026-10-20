# yupiter - installation
#
# "make install" honours DESTDIR, so a package builder can install into
# a staging tree:
#
#     make install DESTDIR=/tmp/stage
#
# The prefix is fixed when ./configure runs; it is not a make variable
# that can be overridden at install time, because the package metadata
# is generated from it.

.PHONY: install uninstall install-headers install-libraries \
        install-metadata

install: all install-headers install-libraries install-metadata

install-headers:
	$(INSTALL) -d $(DESTDIR)$(INCLUDEDIR)
	$(INSTALL) -m 644 include/yupiter.h include/yupiter.hpp \
	    $(DESTDIR)$(INCLUDEDIR)

install-libraries:
ifeq ($(BUILD_STATIC),yes)
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 644 $(STATIC_LIB) $(DESTDIR)$(LIBDIR)/libyupiter.a
endif
ifeq ($(BUILD_SHARED),yes)
	$(INSTALL) -d $(DESTDIR)$(LIBDIR)
	$(INSTALL) -m 755 $(SHARED_LIB) \
	    $(DESTDIR)$(LIBDIR)/$(SHARED_REAL_NAME)
	ln -sf $(SHARED_REAL_NAME) $(DESTDIR)$(LIBDIR)/$(SHARED_SONAME_NAME)
	ln -sf $(SHARED_REAL_NAME) $(DESTDIR)$(LIBDIR)/$(SHARED_LINK_NAME)
endif

install-metadata: $(PACKAGE_FILES)
	$(INSTALL) -d $(DESTDIR)$(PKGCONFIGDIR)
	$(INSTALL) -m 644 $(PC_FILE) $(DESTDIR)$(PKGCONFIGDIR)/yupiter.pc
	$(INSTALL) -d $(DESTDIR)$(CMAKEDIR)
	$(INSTALL) -m 644 $(CMAKE_CONFIG) \
	    $(DESTDIR)$(CMAKEDIR)/yupiter-config.cmake
	$(INSTALL) -m 644 $(CMAKE_VERSION) \
	    $(DESTDIR)$(CMAKEDIR)/yupiter-config-version.cmake

uninstall:
	rm -f $(DESTDIR)$(INCLUDEDIR)/yupiter.h
	rm -f $(DESTDIR)$(INCLUDEDIR)/yupiter.hpp
	rm -f $(DESTDIR)$(LIBDIR)/libyupiter.a
	rm -f $(DESTDIR)$(LIBDIR)/$(SHARED_LINK_NAME)
	rm -f $(DESTDIR)$(LIBDIR)/$(SHARED_SONAME_NAME)
	rm -f $(DESTDIR)$(LIBDIR)/$(SHARED_REAL_NAME)
	rm -f $(DESTDIR)$(PKGCONFIGDIR)/yupiter.pc
	rm -f $(DESTDIR)$(CMAKEDIR)/yupiter-config.cmake
	rm -f $(DESTDIR)$(CMAKEDIR)/yupiter-config-version.cmake
	-rmdir $(DESTDIR)$(CMAKEDIR) $(DESTDIR)$(PKGCONFIGDIR) 2>/dev/null || true
