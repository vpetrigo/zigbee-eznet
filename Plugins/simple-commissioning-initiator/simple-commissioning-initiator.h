#ifndef SIMPLE_COMMISSIONING_PLUGIN_H
#define SIMPLE_COMMISSIONING_PLUGIN_H

#include <stdbool.h>
#include <stdint.h>
#include "app/framework/include/af.h"

/*! Commisioning start functions */
EmberStatus SimpleCommissioningStart(uint8_t endpoint, bool is_server,
                                     const uint16_t *clusters, uint8_t length);

#endif  // SIMPLE_COMMISSIONING_PLUGIN_H