#pragma once
#include <stdint.h>

void ui_init(void);
void ui_play_idle(void);          /* switch to idle GIF (loops forever) */
void ui_play_shake(void);         /* switch to shake GIF (plays once, then auto idle) */
void ui_tick(void);               /* drive from main loop: decode next frame + battery overlay */
bool ui_shake_playing(void);      /* is the shake animation still playing */
