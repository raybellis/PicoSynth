#pragma once

// Audio parameters arrive as CONFIG_* compile definitions from
// CMakeLists.txt; everything below is derived from them, or is a
// tuning constant that used to live inside utils/data.js.

#define SAMPLE_RATE		CONFIG_SAMPLE_RATE
#define BUFFER_SIZE		CONFIG_BUFFER_SIZE

// phase is 16.16 within a WAVE_MAX-sized space
#define WAVE_SHIFT		CONFIG_WAVE_SHIFT
#define WAVE_LEN		(1 << WAVE_SHIFT)
#define WAVE_MAX		(0x10000 * WAVE_LEN)

//--------------------------------------------------------------------+
// Filter tables
//--------------------------------------------------------------------+

// svf_table is indexed in units of 1/SVF_STEPS of a semitone over 128
// MIDI notes.  only 128 of those entries are reachable through
// cutoff_table today; the finer resolution is there for a filter
// envelope to sweep through.  1/16 semitone is 3.1 cents worst case,
// below anything audible as a cutoff error
#define SVF_STEPS		16
#define SVF_LEN			(128 * SVF_STEPS)

// the damping factor is 1:15 holding 1/Q.  1.0 is the ceiling: above
// it the filter has gain at DC and clips a full scale voice
#define SVF_Q_LEN		128
#define SVF_Q_MAX		32768			// Q = 1
#define SVF_Q_MIN		2048			// Q = 16

// the sound controllers offset the patch around centre, so the middle
// of each table is what every patch sounds like before anyone touches
// a controller.  both midpoints were tuned by ear - these are the two
// numbers to change if the defaults want moving
#define SVF_Q_MID		1.28f			// Q at the middle of q_table
#define SVF_NOTE_MID	90.0f			// note at the middle of cutoff_table

// cutoff_table stops here rather than at 127 because svf_table
// saturates at Fs/6, which is note 117.8 - mapping above that would
// only waste control travel
#define SVF_NOTE_MAX	118.0f

//--------------------------------------------------------------------+
// Pitch modulation
//--------------------------------------------------------------------+

// power_table holds 2^(r/POWER_LEN) as 1:15 for one octave.  the
// octave below is the same entries read as 1:16, so only the upper
// one is stored - see frequency_modulate().  x is a 14-bit signed
// offset, 8192 units to the octave, hence the shift
#define POWER_BITS		12				// entries per octave, log2
#define POWER_LEN		(1 << POWER_BITS)
#define POWER_SHIFT		(13 - POWER_BITS)
