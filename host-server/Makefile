.PHONY: main
main: build sync

sync:
	rsync target/armv7-unknown-linux-gnueabihf/debug/host-server ${SSH_HOST}:/home/pi/

build:
	cargo build --target=armv7-unknown-linux-gnueabihf 