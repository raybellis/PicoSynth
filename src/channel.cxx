#include "channel.h"
#include "midi.h"

Channel::Channel() {
	controls = { 0, };
	controls[volume] = 100;
}
