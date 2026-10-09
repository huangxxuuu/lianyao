#pragma once

#include "potion_model.h"
#include <stdbool.h>

bool potion_store_init(potion_player_t *player);
bool potion_store_request_save(const potion_player_t *player);
bool potion_store_has_error(void);
