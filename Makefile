BUILD_DIR ?= build
HTTP_SERVER ?= POCO
HTTP_CLIENT ?= POCO
SOCKET ?= POCO
TLS ?= ON
LOG ?= SPDLOG
JSON ?= NLOHMANN
XML ?= PUGIXML
ZIP ?= ON
FS ?= STD
FFI ?= LIBFFI
CONFIG ?= Release
SCRIPT ?= apps/lua/examples/server.lua
PORT ?= 3000

.PHONY: configure build run clean test format zip deps help wasm app-wasm

help:
	@echo "Varn commands:"
	@echo "  make configure       Configure CMake"
	@echo "  make build           Build varn"
	@echo '  make run             Run $$SCRIPT (default: apps/lua/examples/server.lua)'
	@echo "  make test            Run sample curl commands"
	@echo "  make clean           Remove build dir"
	@echo "  make format          Format all C/C++/ObjC sources"
	@echo "  make zip             Create varn.zip"
	@echo "  make wasm            Build varn_wasm with Emscripten (emcmake on PATH; CMake forces wasm driver set — docs/build.md#emscripten-wasm)"
	@echo "  make app-wasm        Copy wasm into apps/wasm and Vite-build the browser shell (dist/)"
	@echo "Variables: HTTP_SERVER HTTP_CLIENT SOCKET TLS LOG JSON XML ZIP FS FFI SCRIPT PORT (see docs/build.md)"

configure:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=$(CONFIG) \
		-DVARN_HTTP_SERVER_DRIVER=$(HTTP_SERVER) \
		-DVARN_HTTP_CLIENT_DRIVER=$(HTTP_CLIENT) \
		-DVARN_SOCKET_DRIVER=$(SOCKET) \
		-DVARN_ENABLE_TLS=$(TLS) \
		-DVARN_LOG_DRIVER=$(LOG) -DVARN_JSON_DRIVER=$(JSON) -DVARN_XML_DRIVER=$(XML) \
		-DVARN_ENABLE_ZIP=$(ZIP) \
		-DVARN_FS_DRIVER=$(FS) \
		-DVARN_FFI_DRIVER=$(FFI)

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG) -j

run: build
	VARN_PORT=$(PORT) ./$(BUILD_DIR)/bin/varn $(SCRIPT)

test:
	curl -i http://localhost:$(PORT)/
	curl -i http://localhost:$(PORT)/api/hello?name=Paulo
	curl -i -X POST http://localhost:$(PORT)/api/echo -H 'content-type: application/json' -d '{"ok":true}'

clean:
	rm -rf $(BUILD_DIR)

format:
	find src -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.c" -o -name "*.h" -o -name "*.m" -o -name "*.mm" \) | xargs clang-format -i

ROOT_NAME := $(notdir $(abspath .))

zip:
	cd .. && zip -r varn.zip $(ROOT_NAME) -x '$(ROOT_NAME)/build/*' '$(ROOT_NAME)/.git/*' '$(ROOT_NAME)/apps/wasm/node_modules/*' '$(ROOT_NAME)/apps/wasm/dist/*'

wasm:
	emcmake cmake -B $(BUILD_DIR)/wasm -S . -DCMAKE_BUILD_TYPE=$(CONFIG) \
		-DVARN_ENABLE_ZIP=$(ZIP)
	cmake --build $(BUILD_DIR)/wasm --config $(CONFIG) -j

app-wasm: wasm
	cd apps/wasm && VARN_WASM_DIR=../../$(BUILD_DIR)/wasm/bin npm install && VARN_WASM_DIR=../../$(BUILD_DIR)/wasm/bin npm run build
