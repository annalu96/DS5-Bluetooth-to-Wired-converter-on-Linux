#ifndef DS5_BRIDGE_AUDIO_H
#define DS5_BRIDGE_AUDIO_H

#include <cstdint>

void audio_init();
void audio_deinit();

// PCM data is now read from the ALSA device created by f_uac1,
// managed internally by the audio thread. No external call needed.

void set_headset(bool state);

#endif //DS5_BRIDGE_AUDIO_H
