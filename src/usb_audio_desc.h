#pragma once

// A two-channel UAC2 input descriptor.
//
// TinyUSB ships TUD_AUDIO_MIC_ONE_CH_DESCRIPTOR and a four-channel
// one, but nothing between, so stereo has to be assembled here.  This
// is the one-channel macro with four substitutions:
//
//   - the feature unit becomes TWO_CHANNEL, which needs a control
//     word per channel rather than one
//   - the input terminal declares two logical channels
//   - the AS interface declares two physical channels
//   - both carry a front left/right channel configuration instead of
//     "non predefined", so the host knows which way round they are
//
// and the length in the class-specific AC header has to follow the
// feature unit, or the host stops parsing partway through.
//
// The endpoint is marked asynchronous, which is what lets the synth
// keep its own sample clock: the crystal and the host's frame clock
// drift, and the host resamples to absorb it.  That is standard for
// a USB microphone and is why no feedback endpoint is needed.

// The length lives in tusb_config.h as CFG_TUD_AUDIO_FUNC_1_DESC_LEN
// - the driver needs it before this header could be included, and one
// definition is better than two that can drift.

#define TUD_AUDIO_MIC_TWO_CH_STEREO_CFG \
	(AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT)

// mute only, and no volume.  every control declared here is one the
// host will query and expect answered, and a volume slider that moved
// nothing would be worse than no slider - the synth's level is MIDI
// CC 7's job
#define TUD_AUDIO_MIC_TWO_CH_FU_CTRL \
	(AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS)

#define TUD_AUDIO_MIC_TWO_CH_DESCRIPTOR(_itfnum, _stridx, _nBytesPerSample, _nBitsUsedPerSample, _epin, _epsize) \
	/* Interface Association */\
	TUD_AUDIO_DESC_IAD(/*_firstitf*/ _itfnum, /*_nitfs*/ 0x02, /*_stridx*/ 0x00),\
	/* Standard AC Interface */\
	TUD_AUDIO_DESC_STD_AC(/*_itfnum*/ _itfnum, /*_nEPs*/ 0x00, /*_stridx*/ _stridx),\
	/* Class-Specific AC Interface Header - _totallen must match the units below */\
	TUD_AUDIO_DESC_CS_AC(/*_bcdADC*/ 0x0200, /*_category*/ AUDIO_FUNC_MICROPHONE,\
		/*_totallen*/ TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN\
			+ TUD_AUDIO_DESC_OUTPUT_TERM_LEN + TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN,\
		/*_ctrl*/ AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),\
	/* Clock Source - internal and fixed; drift is the host's problem, see above */\
	TUD_AUDIO_DESC_CLK_SRC(/*_clkid*/ 0x04, /*_attr*/ AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,\
		/*_ctrl*/ (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS),\
		/*_assocTerm*/ 0x01, /*_stridx*/ 0x00),\
	/* Input Terminal - two logical channels, front left and right */\
	TUD_AUDIO_DESC_INPUT_TERM(/*_termid*/ 0x01, /*_termtype*/ AUDIO_TERM_TYPE_IN_GENERIC_MIC,\
		/*_assocTerm*/ 0x03, /*_clkid*/ 0x04, /*_nchannelslogical*/ 0x02,\
		/*_channelcfg*/ TUD_AUDIO_MIC_TWO_CH_STEREO_CFG, /*_idxchannelnames*/ 0x00,\
		/*_ctrl*/ AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS, /*_stridx*/ 0x00),\
	/* Output Terminal */\
	TUD_AUDIO_DESC_OUTPUT_TERM(/*_termid*/ 0x03, /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,\
		/*_assocTerm*/ 0x01, /*_srcid*/ 0x02, /*_clkid*/ 0x04, /*_ctrl*/ 0x0000, /*_stridx*/ 0x00),\
	/* Feature Unit - master plus one control word per channel */\
	TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(/*_unitid*/ 0x02, /*_srcid*/ 0x01,\
		/*_ctrlch0master*/ TUD_AUDIO_MIC_TWO_CH_FU_CTRL,\
		/*_ctrlch1*/ TUD_AUDIO_MIC_TWO_CH_FU_CTRL,\
		/*_ctrlch2*/ TUD_AUDIO_MIC_TWO_CH_FU_CTRL, /*_stridx*/ 0x00),\
	/* AS Interface alt 0 - zero bandwidth, the host's idle setting */\
	TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum)+1), /*_altset*/ 0x00,\
		/*_nEPs*/ 0x00, /*_stridx*/ 0x00),\
	/* AS Interface alt 1 - streaming */\
	TUD_AUDIO_DESC_STD_AS_INT(/*_itfnum*/ (uint8_t)((_itfnum)+1), /*_altset*/ 0x01,\
		/*_nEPs*/ 0x01, /*_stridx*/ 0x00),\
	/* Class-Specific AS Interface - two physical channels */\
	TUD_AUDIO_DESC_CS_AS_INT(/*_termid*/ 0x03, /*_ctrl*/ AUDIO_CTRL_NONE,\
		/*_formattype*/ AUDIO_FORMAT_TYPE_I, /*_formats*/ AUDIO_DATA_FORMAT_TYPE_I_PCM,\
		/*_nchannelsphysical*/ 0x02, /*_channelcfg*/ TUD_AUDIO_MIC_TWO_CH_STEREO_CFG,\
		/*_stridx*/ 0x00),\
	/* Type I Format */\
	TUD_AUDIO_DESC_TYPE_I_FORMAT(_nBytesPerSample, _nBitsUsedPerSample),\
	/* Isochronous IN endpoint, asynchronous */\
	TUD_AUDIO_DESC_STD_AS_ISO_EP(/*_ep*/ _epin,\
		/*_attr*/ (uint8_t)((uint8_t)TUSB_XFER_ISOCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS | (uint8_t)TUSB_ISO_EP_ATT_DATA),\
		/*_maxEPsize*/ _epsize, /*_interval*/ 0x01),\
	/* Class-Specific AS Isochronous Endpoint */\
	TUD_AUDIO_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,\
		/*_ctrl*/ AUDIO_CTRL_NONE,\
		/*_lockdelayunit*/ AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED,\
		/*_lockdelay*/ 0x0000)
