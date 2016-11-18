// *******************************************************************
// * simple-commissioning-initiator.c
// *
// * Author: Vladimir Petrigo (pve@efo.ru)
// *
// *******************************************************************

// Typedefs for Simple Commissioning plugin
#include "simple-commissioning-initiator.h"
#include "simple-commissioning-initiator-internal.h"
#include "simple-commissioning-td.h"

/*! Global for storing current device's commissioning information
 */
DevCommClusters_t dev_comm_session;

/*! Helper inline function for init DeviceCommissioningClusters struct */
static inline void InitDeviceCommissionInfo(DevCommClusters_t *dcc,
                                            const uint8_t ep,
                                            const bool is_server,
                                            const uint16_t *clusters_arr,
                                            const uint8_t clusters_arr_len) {
  dcc->clusters = clusters_arr;
  dcc->ep = ep;
  dcc->clusters_arr_len = clusters_arr_len;
  // Get a network index for the requested endpoint
  dcc->network_index = emberAfNetworkIndexFromEndpoint(ep);
  dcc->is_server = is_server;
}

EmberStatus SimpleCommissioningStart(uint8_t endpoint, bool is_server,
                                     const uint16_t *clusters, uint8_t length) {
  if (!clusters || !length) {
    // meaningless call if ClusterID array was not passed or its length
    // is zero
    return EMBER_BAD_ARGUMENT;
  }
  emberAfDebugPrintln("DEBUG: Call for starting commissioning");
  if (length > emberBindingTableSize) {
    // passed more clusters than the binding table may handle
    // TODO: may be it is worth to track available entries to write in
    // the binding table
    emberAfDebugPrint("Warning: ask for bind 0x%X clusters. ", length);
    emberAfDebugPrintln("Binding table size is 0x%X", emberBindingTableSize);
  }
  if (CommissioningStateMachineStatus() != SC_EZ_STOP) {
    // quite implicit, but if our state machine runs then network
    // probably busy
    return EMBER_NETWORK_BUSY;
  }

  InitDeviceCommissionInfo(&dev_comm_session, endpoint, is_server, clusters,
                           length);
  // Wake up our state machine
  emberEventControlSetActive(StateMachineEvent);

  return EMBER_SUCCESS;
}
