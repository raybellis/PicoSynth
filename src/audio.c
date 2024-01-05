#include "pico/stdlib.h"
#include "audio.h"

struct audio_buffer_pool *audio_init() {

	static audio_format_t audio_format = {
			.format = AUDIO_BUFFER_FORMAT_PCM_S16,
			.sample_freq = SAMPLE_RATE,
			.channel_count = 2,
	};

	static struct audio_buffer_format producer_format = {
			.format = &audio_format,
			.sample_stride = 4
	};

	struct audio_buffer_pool *producer_pool =
		audio_new_producer_pool(&producer_format, 3, BUFFER_SIZE);
	bool __unused ok;
	const struct audio_format *output_format;

	struct audio_i2s_config config = {
			.data_pin = PICO_AUDIO_I2S_DATA_PIN,
			.clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
			.dma_channel = 0,
			.pio_sm = 0,
	};

	output_format = audio_i2s_setup(&audio_format, &config);
	if (!output_format) {
		panic("PicoAudio: Unable to open audio device.\n");
	}

	ok = audio_i2s_connect(producer_pool);
	assert(ok);
	audio_i2s_set_enabled(true);

#if CONFIG_HW_PICOADK == 1
	// set gpio 25 (dac soft mute) to output and set to 1 (unmute)
	gpio_init(25);
	gpio_set_dir(25, GPIO_OUT);
	gpio_put(25, 1);
#endif

	return producer_pool;
}
