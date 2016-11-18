#ifndef SIMPLE_COMMISSIONING_INITIATOR_BUFFER_H
#define SIMPLE_COMMISSIONING_INITIATOR_BUFFER_H

#include "app/framework/include/af.h"
#include "simple-commissioning-td.h"

/// Initialize queue
void InitQueue(void);
/// Function for adding initial info about a remote device
///
/// It is necessary to pass only remote device's short ID and endpoint
bool AddInDeviceDescriptor(const EmberNodeId short_id, const uint8_t endpoint);
/// Function for getting the top remote device's descriptor
MatchDescriptorReq_t *GetTopInDeviceDescriptor(void);
/// Delete the top descriptor
void PopInDeviceDescriptor(void);
/// Get queue size
uint8_t GetQueueSize(void);

#endif  // SIMPLE_COMMISSIONING_INITIATOR_BUFFER_H