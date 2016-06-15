// *******************************************************************
// * simple-commissioning.c
// *
// * Author: Vladimir Petrigo (pve@efo.ru)
// * 
// *******************************************************************

#include "../../include/af.h"

// events declaration
EmberEventControl emberAfPluginSimpleCommissioningStateMachineEventControl;

// local typedefs

// structure for handling incoming matching requests
typedef struct {
  // Node short ID
  EmberNodeId source,
  // Node endpoint
  uint8_t source_ep
} MatchDescriptorReq_t;

static MatchDescriptorReq_t matching_node;

static void MatchDescriptorRequest(void) {
  
}

static void 

/** @brief Identify Cluster Identify Query Response
 *
 * 
 *
 * @param timeout   Ver.: always
 */
boolean emberAfIdentifyClusterIdentifyQueryResponseCallback(int16u timeout) {
  // ignore broadcasts from yourself and from devices that are not
  // in the identifying state
  if (emberAfGetNodeId() != emberAfCurrentCommand()->source) {
    // nothing to do if it is too late for commissioning
    if (timeout != 0) {
      matching_node.source = emberAfCurrentCommand()->source;
      matching_node.source_ep = emberAfCurrentCommand()->apsFrame->sourceEndpoint;
    }
  }
  
  return TRUE;
}