#ifndef SIMPLE_COMMISSIONING_INITIATOR_INTERNAL_H
#define SIMPLE_COMMISSIONING_INITIATOR_INTERNAL_H

#include "app/framework/include/af.h"

/// External variables used by internal and/or public implementation
extern DevCommClusters_t dev_comm_session;
extern EmberEventControl emberAfPluginSimpleCommissioningInitiatorStateMachineEventControl;

/// \typedef Handy typedef for the long plugin's event name
#define StateMachineEvent emberAfPluginSimpleCommissioningInitiatorStateMachineEventControl

/// Public interface for interface for plugin's internal implementation
CommissioningState_t CommissioningStateMachineStatus(void);

#endif // SIMPLE_COMMISSIONING_INITIATOR_INTERNAL_H