BUILD_DIR := build
TARGET    := BeyMeter

.PHONY: build uf2 hex clean

build:
	$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)

uf2: build
	@cp $(BUILD_DIR)/$(TARGET).uf2 .
	@echo "$(TARGET).uf2 ready"

hex: build
	@cp $(BUILD_DIR)/$(TARGET).hex .
	@echo "$(TARGET).hex ready"

clean:
	$(MAKE) -C $(BUILD_DIR) clean
	@rm -f $(TARGET).uf2 $(TARGET).hex
