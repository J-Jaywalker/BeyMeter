BUILD_DIR := build
TARGET    := BeyMeter

.PHONY: build uf2 hex clean

build: _cmake
	$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)

uf2: _clean _cmake
	$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)
	@cp $(BUILD_DIR)/$(TARGET).uf2 .
	@echo "$(TARGET).uf2 ready"

hex: _clean _cmake
	$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)
	@cp $(BUILD_DIR)/$(TARGET).hex .
	@echo "$(TARGET).hex ready"

clean: _clean

_clean:
	@rm -rf $(BUILD_DIR)
	@rm -f $(TARGET).uf2 $(TARGET).hex
	@mkdir -p $(BUILD_DIR)

_cmake:
	@cmake -S . -B $(BUILD_DIR)
