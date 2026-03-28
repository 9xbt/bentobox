.PHONY: setup resetup build clean install uninstall headers

ARCH := x86_64

setup:
	cd lib/mlibc && \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=shared -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/base/$(ARCH)/usr/	

resetup:
	cd lib/mlibc && \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=shared -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/base/$(ARCH)/usr/ --wipe

build:
	cd lib/mlibc && \
	PATH="$(CURDIR)/util/build/bin:$$PATH" \
	ninja -C build-$(ARCH)

clean:
	cd lib/mlibc && ninja -C build-$(ARCH) clean

install:
	cd lib/mlibc && \
	ninja -C build-$(ARCH) install
	mkdir -p $(CURDIR)/build/base/$(ARCH)/etc
	mkdir -p $(CURDIR)/build/base/$(ARCH)/usr/include/linux
	cp -r /usr/include/linux $(CURDIR)/build/base/$(ARCH)/usr/include/
	cp -r /usr/include/asm $(CURDIR)/build/base/$(ARCH)/usr/include/
	cp -r /usr/include/asm-generic $(CURDIR)/build/base/$(ARCH)/usr/include/asm-generic
	cp /etc/localtime $(CURDIR)/build/base/$(ARCH)/etc/

uninstall:
	rm -rf build/mlibc/$(ARCH)/

headers:
	cd lib/mlibc && \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=true \
		--prefix=$(CURDIR)/build/base/$(ARCH)/usr/
	cd lib/mlibc && ninja -C build-$(ARCH) install
	mkdir -p $(CURDIR)/build/base/$(ARCH)/usr/lib
	touch $(CURDIR)/build/base/$(ARCH)/usr/lib/libc.so
	touch $(CURDIR)/build/base/$(ARCH)/usr/lib/libm.so
	touch $(CURDIR)/build/base/$(ARCH)/usr/lib/libpthread.so
