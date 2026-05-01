//
// Created by awalol on 2026/3/5.
// Refactored for Linux (Fase 2)
//

#ifndef DS5_BRIDGE_AUDIO_H
#define DS5_BRIDGE_AUDIO_H

#include <cstdint>

void audio_init();
void audio_deinit();
void set_headset(bool state);
void audio_receive_pcm(const int16_t* raw, uint32_t bytes_read);

#endif //DS5_BRIDGE_AUDIO_H
