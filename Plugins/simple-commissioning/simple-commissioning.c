// *******************************************************************
// * simple-commissioning.c
// *
// * Author: Vladimir Petrigo (pve@efo.ru)
// * 
// *******************************************************************

// Typedefs for Simple Commissioning plugin
#include "simple-commissioning.h"
#include "simple-commissioning-td.h"

/*! Simple Commissioning Plugin event declaration */
EmberEventControl emberAfPluginSimpleCommissioningStateMachineEventControl;

/*! \typedef Handy typedef for the long plugin's event name */
#define StateMachineEvent emberAfPluginSimpleCommissioningStateMachineEventControl

/*! Simple Commissioning Plugin event handler declaration*/
void emberAfPluginSimpleCommissioningStateMachineEventHandler(void);

/// Transit state to SC_EZ_START
static CommissioningState_t StartCommissioning(void);
/// TODO: There must be checks whether a device is in a network or not
static CommissioningState_t CheckNetwork(void);
static CommissioningState_t BroadcastIdentifyQuery(void);
static CommissioningState_t GotIdentifyRespQuery(void);
static CommissioningState_t StopCommissioning(void);
static CommissioningState_t CheckClusters(void);
/// Transit state to SC_EZ_BIND
static CommissioningState_t SetBinding(void);
static CommissioningState_t UnknownState(void);

/*! Callback for Service Discovery Request */
static void ProcessServiceDiscovery(const EmberAfServiceDiscoveryResult *result);

/*! State Machine Table */
static const SMTask_t sm_transition_table[] = {
  {SC_EZ_STOP, SC_EZEV_START_COMMISSIONING, &StartCommissioning},
  {SC_EZ_START, SC_EZEV_CHECK_NETWORK, &CheckNetwork},
  {SC_EZ_START, SC_EZEV_BCAST_IDENT_QUERY, &BroadcastIdentifyQuery},
  {SC_EZ_WAIT_IDENT_RESP, SC_EZEV_GOT_RESP, &GotIdentifyRespQuery},
  {SC_EZ_WAIT_IDENT_RESP, SC_EZEV_TIMEOUT, &StopCommissioning},
  {SC_EZ_DISCOVER, SC_EZEV_CHECK_CLUSTERS, &CheckClusters},
  {SC_EZ_DISCOVER, SC_EZEV_BAD_DISCOVER, &StopCommissioning},
  {SC_EZ_MATCH, SC_EZEV_NOT_MATCHED, &StopCommissioning},
  {SC_EZ_BIND, SC_EZEV_BIND, &SetBinding},
  {SC_EZ_BIND, SC_EZEV_BINDING_DONE, &StopCommissioning},
  {SC_EZ_UNKNOWN, SC_EZEV_UNKNOWN, &UnknownState}
};

#define TRANSIT_TABLE_SIZE() (sizeof(sm_transition_table) / sizeof(sm_transition_table[0]))

/*! Global for storing current device's commissioning information
 */
DevCommClusters_t dev_comm_session;

/*! Global for storing next state machine transition
 */
SMNext_t next_transition = {
  SC_EZ_UNKNOWN, SC_EZEV_UNKNOWN
};

/*! Global for storing incoming device's info (Short ID, EUI64, endpoint)
 */
MatchDescriptorReq_t incoming_conn;

/*! Helper inline function for init DeviceCommissioningClusters struct */
static inline void InitDCC(DevCommClusters_t *dcc, const uint8_t ep, const bool is_server,
                      const uint16_t *clusters_arr, const uint8_t clusters_arr_len) {
  dcc->clusters = clusters_arr;
  dcc->ep = ep;
  dcc->clusters_arr_len = clusters_arr_len;
  // Get a network index for the requested endpoint
  dcc->network_index = emberAfNetworkIndexFromEndpoint(ep);
  dcc->is_server = is_server;
}

/*! Helper inline function for getting next state */
static inline CommissioningState_t GetNextState(void) {
  return next_transition.next_state;
}

/*! Helper inline function for getting next event */
static inline CommissioningEvent_t GetNextEvent(void) {
  return next_transition.next_event;
}

/*! Helper inline function for setting next state */
static inline void SetNextState(const CommissioningState_t cstate) {
  next_transition.next_state = cstate;
}

/*! Helper inline function for setting next event */
static inline void SetNextEvent(const CommissioningEvent_t cevent) {
  next_transition.next_event = cevent;
}

/*! Helper inline function for setting an incoming connection info
    during the Identify Query Response
*/
static inline void SetInConnBaseInfo(const EmberNodeId short_id, 
                                     const uint8_t endpoint) {
  incoming_conn.source = short_id;
  incoming_conn.source_ep = endpoint;
}

/*! Helper inline function for setting an incoming connection device's
    clusters list and length of that list
*/
static inline void SetInDevicesClustersInfo(const uint16_t *clusters_list, 
                                     const uint8_t clusters_list_len) {
  incoming_conn.source_cl_arr = clusters_list;
  incoming_conn.source_cl_arr_len = clusters_list_len;
}

/*! State Machine function */
void emberAfPluginSimpleCommissioningStateMachineEventHandler(void) {
  emberEventControlSetInactive(StateMachineEvent);
  // That might happened that ZigBee state machine changed current network
  // So, it is important to switch to the proper network before commissioning
  // state machine might start
  EmberStatus status = emberAfPushNetworkIndex(dev_comm_session.network_index);
  if (status != EMBER_SUCCESS) {
    // TODO: Handle unavailability of switching network
  }
  emberAfDebugPrintln("DEBUG: State Machine");
  // Get state previously set by some handler
  CommissioningState_t cur_state = GetNextState();
  CommissioningEvent_t cur_event = GetNextEvent();
  for (size_t i = 0; i < TRANSIT_TABLE_SIZE(); ++i) {
    if ((cur_state == sm_transition_table[i].state ||
        SC_EZ_UNKNOWN == sm_transition_table[i].state) &&
        ((cur_event == sm_transition_table[i].event) ||
        SC_EZEV_UNKNOWN == sm_transition_table[i].event)) {
      // call handler which set the next_state on return and
      // next_event inside itself
      SetNextState((sm_transition_table[i].handler)());
      break;
    }
  }
  
  // Don't forget to pop Network Index
  status = emberAfPopNetworkIndex();
  // sanity check that network will be switch back properly
  EMBER_TEST_ASSERT(status == EMBER_SUCCESS);
}

EmberStatus SimpleCommissioningStart(uint8_t endpoint, 
                                     bool is_server, 
                                     const uint16_t *clusters, 
                                     uint8_t length) {
  if (!clusters || !length) {
    // meaningless call if no ClusterID array was passed or its length
    // is zero
    return EMBER_BAD_ARGUMENT;
  }
  emberAfDebugPrintln("DEBUG: Call for starting commissioning");
  if (length > emberBindingTableSize) {
    // passed more clusters than the binding table may handle
    // TODO: may be it is worth to track available entrise to write in
    // the binding table
    emberAfDebugPrint("Warning: ask for bind 0x%X clusters. ", length);
    emberAfDebugPrintln("Binding table size is 0x%X", emberBindingTableSize);
  }
  
  InitDCC(&dev_comm_session, endpoint, is_server, clusters, length);
  // Current state is SC_EZ_STOP, so for transiting to the next state
  // set up event accordingly to the transmission table
  SetNextState(SC_EZ_STOP);
  SetNextEvent(SC_EZEV_START_COMMISSIONING);
  // Wake up our state machine
  emberEventControlSetActive(StateMachineEvent);
  
  return EMBER_SUCCESS;
}

/** @brief Identify Cluster Identify Query Response
 *
 * 
 *
 * @param timeout   Ver.: always
 */
boolean emberAfIdentifyClusterIdentifyQueryResponseCallback(int16u timeout) {
  // TODO: !!! IMPORTANT !!! add  handling for several responses
  // now the state machine might be broken as we use only one global variable
  // for storing incoming connection information like Short ID and endpoint
  // 
  // ignore broadcasts from yourself and from devices that are not
  // in the identifying state
  const EmberAfClusterCommand * const current_cmd = emberAfCurrentCommand();
  if (emberAfGetNodeId() != current_cmd->source && timeout != 0) {
    emberAfDebugPrintln("DEBUG: Got ID Query response");
    emberAfDebugPrintln("DEBUG: Sender 0x%X", emberAfCurrentCommand()->source);
    // Store information about endpoint and short ID of the incoming response
    // for further processing in the Matching (SC_EZ_MATCH) state
    SetInConnBaseInfo(current_cmd->source,
                      current_cmd->apsFrame->sourceEndpoint);
    // ID Query received -> go to the discover state for getting clusters info
    SetNextState(SC_EZ_DISCOVER);
    SetNextEvent(SC_EZEV_CHECK_CLUSTERS);
    
    emberAfSendImmediateDefaultResponse(EMBER_ZCL_STATUS_SUCCESS);
    emberEventControlSetActive(StateMachineEvent);
  }
  
  return TRUE;
}

/*! State Machine handlers implementation */
static CommissioningState_t StartCommissioning(void) {
  emberAfDebugPrintln("DEBUG: Commissioning Start");
  // TODO: here we might add some sanity check like cluster existense
  // or something like that, but now just start commissioning process
  SetNextEvent(SC_EZEV_CHECK_NETWORK);
  emberEventControlSetActive(StateMachineEvent);
  
  return SC_EZ_START;  
}

static CommissioningState_t CheckNetwork(void) {
  emberAfDebugPrintln("DEBUG: Check Network state");
  
  CommissioningState_t next_st = SC_EZ_UNKNOWN;
  CommissioningEvent_t next_ev = SC_EZEV_UNKNOWN;
  EmberNetworkStatus nw_status = emberNetworkState();
  
  if (nw_status == EMBER_JOINED_NETWORK) {
    next_st = SC_EZ_START;
    next_ev = SC_EZEV_BCAST_IDENT_QUERY;    
  }
  else if (nw_status == EMBER_NO_NETWORK) {
    // Form or join available network, but now just stop commissioning
  }
  
  // Send Permit Join broadcast to the current network
  // in case of ZED nothing will happen
  // TODO: Make this hadrcoded value as plugin's option
  emberAfPermitJoin(180, TRUE);
  // if the device is in the network continue commissioning
  // by sending Identify Query
  SetNextEvent(next_ev);
  emberEventControlSetActive(StateMachineEvent);
  
  return next_st;
}

static CommissioningState_t BroadcastIdentifyQuery(void) {
  emberAfDebugPrintln("DEBUG: Broadcast ID Query");
  // Make Identify cluster's command to send an Identify Query
  emberAfFillCommandIdentifyClusterIdentifyQuery();
  emberAfSetCommandEndpoints(dev_comm_session.ep, EMBER_BROADCAST_ENDPOINT);
  // Broadcast Identify Query
  EmberStatus status = emberAfSendCommandBroadcast(EMBER_SLEEPY_BROADCAST_ADDRESS);

  if (status != EMBER_SUCCESS) {
    // Exceptional case. Stop commissioning
    SetNextEvent(SC_EZEV_UNKNOWN);
    
    return SC_EZ_UNKNOWN;
  }
  
  // Schedule event for awaiting for responses for 1 second
  // TODO: Set hardcoded value as plugin's option parameter as optional value
  emberEventControlSetDelayMS(StateMachineEvent, 1000);
  // If Identify Query responses won't be received state machine just will call
  // Timeout handler
  SetNextEvent(SC_EZEV_TIMEOUT);
  
  return SC_EZ_WAIT_IDENT_RESP;
}

static CommissioningState_t UnknownState(void) {
  emberAfDebugPrintln("DEBUG: Unknown State handler");
  
  return SC_EZ_STOP;
}

static CommissioningState_t GotIdentifyRespQuery(void) {
  emberAfDebugPrintln("DEBUG: Got Identify Response Query handler");
    
  return SC_EZ_UNKNOWN;
}

static CommissioningState_t CheckClusters(void) {
  emberAfDebugPrintln("DEBUG: Check Clusters handler");
  // ask a responded device for providing with info about clusters and call
  // the callback
  EmberStatus status = emberAfFindClustersByDeviceAndEndpoint(incoming_conn.source,
                                                              incoming_conn.source_ep,
                                                              ProcessServiceDiscovery);
  
  // Nothing to do here with states as the next event will become clear
  // during the ProcessServiceDiscovery callback call
  SetNextEvent(SC_EZEV_UNKNOWN);
  
  return (status != EMBER_SUCCESS) ? SC_EZ_UNKNOWN : SC_EZ_DISCOVER;
}

/*! Callback for Service Discovery Request */
static void ProcessServiceDiscovery(const EmberAfServiceDiscoveryResult *result) {
  // if we get a matche or a default response handle it
  // otherwise stop commissioning or go to the next incoming device
  if (emberAfHaveDiscoveryResponseStatus(result->status)) {
    EmberAfClusterList *discovered_clusters = (EmberAfClusterList *) result->responseData;
    
    // if our device requested to bind to server clusters (is_server parameter
    // during the SimpleCommissioningStart was FALSE) -> use inClusterList of
    // the incoming device's response
    // otherwise -> outClusterList (as our device has server clusters)
    const uint16_t *inc_clusters_arr = (dev_comm_session.is_server) ? 
      discovered_clusters->outClusterList :
      discovered_clusters->inClusterList;
    // get correct lenght of the incoming clusters array
    const uint8_t inc_clusters_arr_len = (dev_comm_session.is_server) ? 
      discovered_clusters->outClusterCount :
      discovered_clusters->inClusterCount;
    // update our incoming device structure with information about clusters
    SetInDevicesClustersInfo(inc_clusters_arr, inc_clusters_arr_len);
    
    SetNextEvent(SC_EZEV_BAD_DISCOVER);
  }
  else {
    SetNextEvent(SC_EZEV_BAD_DISCOVER);
  }
}

static CommissioningState_t SetBinding(void) {
  return SC_EZ_UNKNOWN;
}

static CommissioningState_t StopCommissioning(void) {
  emberAfDebugPrintln("DEBUG: Stop commissioning");
  emberAfDebugPrintln("Current state is 0x%X", GetNextState());
  SetNextEvent(SC_EZEV_UNKNOWN);
  
  return SC_EZ_UNKNOWN;
}
