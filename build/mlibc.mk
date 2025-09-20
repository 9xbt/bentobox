.PHONY: setup resetup build clean install

ARCH := x86_64

setup:
	cd lib/mlibc && \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/mlibc/$(ARCH)/

resetup:
	cd lib/mlibc && \
	meson setup build-$(ARCH) --cross-file ../../build/crossfile-$(ARCH).txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/mlibc/$(ARCH)/ --wipe

build:
	cd lib/mlibc && ninja -C build-$(ARCH)

clean:
	cd lib/mlibc && ninja -C build-$(ARCH) clean

install:
	cd lib/mlibc && ninja -C build-$(ARCH) install

uninstall:
	rm -rf build/mlibc/$(ARCH)/