.PHONY: setup resetup build clean install

setup:
	cd lib/mlibc && \
	meson setup build --cross-file ../../build/crossfile.txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/mlibc/

resetup:
	cd lib/mlibc && \
	meson setup build --cross-file ../../build/crossfile.txt -Dheaders_only=false \
		-Ddefault_library=static -Dbuild_tests=false -Dposix_option=enabled \
		-Dlinux_option=disabled -Dglibc_option=enabled -Dbsd_option=enabled \
		--prefix=$(CURDIR)/build/mlibc/ --wipe

build:
	cd lib/mlibc && ninja -C build

clean:
	cd lib/mlibc && ninja -C build clean

install:
	cd lib/mlibc && ninja -C build install