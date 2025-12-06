.PHONY: setup resetup build clean install

ARCH := x86_64

setup:
	cd lib/mlibc && \
	PATH="$(CURDIR)/util/build/bin:$$PATH" \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/base/$(ARCH)/usr/
	cp /etc/localtime base/etc/localtime

resetup:
	cd lib/mlibc && \
	PATH="$(CURDIR)/util/build/bin:$$PATH" \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/base/$(ARCH)/usr/ --wipe
	cp /etc/localtime base/etc/localtime

build:
	cd lib/mlibc && \
	PATH="$(CURDIR)/util/build/bin:$$PATH" \
	ninja -C build-$(ARCH)

clean:
	cd lib/mlibc && ninja -C build-$(ARCH) clean

install:
	cd lib/mlibc && \
	PATH="$(CURDIR)/util/build/bin:$$PATH" \
	ninja -C build-$(ARCH) install
	mkdir -p $(CURDIR)/build/base/$(ARCH)/usr/include/linux
	cp -r /usr/include/linux $(CURDIR)/build/base/$(ARCH)/usr/include/
	cp -r /usr/include/asm $(CURDIR)/build/base/$(ARCH)/usr/include/
	cp -r /usr/include/asm-generic $(CURDIR)/build/base/$(ARCH)/usr/include/asm-generic

uninstall:
	rm -rf build/mlibc/$(ARCH)/