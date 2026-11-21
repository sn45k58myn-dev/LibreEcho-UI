# Include after the project Makefile to build the observation-only trial.
$(BUILD)/wake_trial.arm.o: tools/wake_trial.c src/adapter/adapter.h src/adapter/voice_dsp.h \
		src/adapter/voice_stream.h src/adapter/wake_worker.h src/json.h src/wake_model.h
	@mkdir -p $(BUILD)
	$(CROSS_COMPILE)$(CC) -std=c99 -O2 -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
		-ffunction-sections -fdata-sections -Wall -Wextra -Wpedantic -Werror -Isrc -c $< -o $@

$(BUILD)/wake-trial-arm32: $(BUILD)/wake_trial.arm.o $(BUILD)/wake_engine_onnx.arm.o \
		$(BUILD)/wake_worker.wake.arm.o $(BUILD)/voice_dsp.wake.arm.o \
		$(BUILD)/voice_stream.wake.arm.o $(BUILD)/wake_model.wake.arm.o $(BUILD)/json.wake.arm.o
	$(CROSS_COMPILE)g++ -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
		-static -static-libstdc++ -static-libgcc -Wl,--gc-sections -o $@ $^ \
		-Wl,--start-group $(WAKE_ORT_ARCHIVES) $(WAKE_ORT_ABSEIL) -Wl,--end-group -lpthread -ldl -lm
