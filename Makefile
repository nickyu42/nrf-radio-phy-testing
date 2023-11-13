.PHONY: main
main: build flash
	pyocd rtt --target nrf52840

.PHONY: test
test: build
	pyocd flash -t nrf52840 -u ${PROBE1} ./build/zephyr/zephyr.elf
	pyocd flash -t nrf52840 -u ${PROBE2} ./build/zephyr/zephyr.elf
	tmux \
		new-session  'pyocd rtt --target nrf52840 -u ${PROBE1}' \; \
		split-window 'pyocd rtt --target nrf52840 -u ${PROBE2}'

.PHONY: rtt
rtt:
	tmux \
		new-session  'pyocd rtt --target nrf52840 -u ${PROBE1}' \; \
		split-window 'pyocd rtt --target nrf52840 -u ${PROBE2}'

.PHONY: dfu
dfu:
	source ./venv/bin/activate && \
	adafruit-nrfutil dfu serial --package ./build/dfu-package.zip -p ${SERIAL_PORT} -b 115200

.PHONY: flash
flash:
	pyocd flash -t nrf52840 ./build/zephyr/zephyr.elf

.PHONY: build
build:
	source ./env.sh && \
	west build \
		--build-dir /Users/nick/dev/west_nrf/radio_test/build /Users/nick/dev/west_nrf/radio_test \
		--board xiao_ble --no-sysbuild -- \
		-DNCS_TOOLCHAIN_VERSION:STRING="NONE" \
		-DCONF_FILE:STRING="/Users/nick/dev/west_nrf/radio_test/prj.conf" \

.PHONY: package
package:
	source ./venv/bin/activate && \
	adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application ./build/zephyr/zephyr.hex ./build/dfu-package.zip
	
.PHONY: clean
clean:
	rm -rf ./build