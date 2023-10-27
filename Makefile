
.PHONY: main
main: build package
	source ./venv/bin/activate && \
	adafruit-nrfutil dfu serial --package ./build/dfu-package.zip -p ${SERIAL_PORT} -b 115200

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