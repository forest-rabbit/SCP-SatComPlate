# Makefile wrapper for waf

all: build

# free free to change this part to suit your requirements
configure:
	./waf configure --disable-examples --disable-tests --enable-modules=satcompute

build:
	./waf build --targets=satcompute

install:
	./waf install

clean:
	./waf clean

distclean:
	./waf distclean
