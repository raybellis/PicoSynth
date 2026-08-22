// I2S output, via pico_audio_i2s from pico-extras.
//
// Everything pico_audio-shaped is confined to this file; audio.h
// exposes only take/give, so a second backend can present the same
// interface without audio_task() knowing which is underneath.

#include "pico/stdlib.h"
#include "pico/audio_i2s.h"

#include "audio.h"

static struct audio_buffer_pool*	pool = NULL;

// the buffer handed out by the last audio_take().  only one may be
// outstanding, so remembering it here is what lets audio_give() take
// no argument
static struct audio_buffer*			current = NULL;

void audio_init(void)
{
	static audio_format_t audio_format = {
			.format = AUDIO_BUFFER_FORMAT_PCM_S16,
			.sample_freq = SAMPLE_RATE,
			.channel_count = 2,
	};

	static struct audio_buffer_format producer_format = {
			.format = &audio_format,
			.sample_stride = 4
	};

	pool = audio_new_producer_pool(&producer_format, 3, BUFFER_SIZE);

	struct audio_i2s_config config = {
			.data_pin = PICO_AUDIO_I2S_DATA_PIN,
			.clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
			.dma_channel = 0,
			.pio_sm = 0,
	};

	if (!audio_i2s_setup(&audio_format, &config)) {
		panic("PicoAudio: Unable to open audio device.\n");
	}

	bool __unused ok = audio_i2s_connect(pool);
	assert(ok);

	audio_i2s_set_enabled(true);

#if CONFIG_HW_PICOADK == 1
	// set gpio 25 (dac soft mute) to output and set to 1 (unmute)
	gpio_init(25);
	gpio_set_dir(25, GPIO_OUT);
	gpio_put(25, 1);
#endif
}

int16_t* audio_take(void)
{
	// non-blocking on purpose; see audio.h
	current = take_audio_buffer(pool, false);

	return current ? (int16_t*)current->buffer->bytes : NULL;
}

void audio_give(void)
{
	current->sample_count = current->max_sample_count;
	give_audio_buffer(pool, current);
	current = NULL;
}
