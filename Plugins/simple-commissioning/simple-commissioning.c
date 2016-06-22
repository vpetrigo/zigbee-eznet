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

/// Functions for handling different states
/// Transit state to SC_EZ_START
static CommissioningState_t StartCommissioning(void);
static CommissioningState_t CheckNetwork(void);
static CommissioningState_t BroadcastIdentifyQuery(void);
static CommissioningState_t StopCommissioning(void);
static CommissioningState_t CheckClusters(void);
static CommissioningState_t MatchingCheck(void);
/// Transit state to SC_EZ_BIND
static CommissioningState_t SetBinding(void);
static CommissioningState_t UnknownState(void);
static CommissioningState_t FormJoinNetwork(void);

/// Functions for working with RemoteSkipClusters
/// Initialize global struct RemoteSkipClusters variable
static inline void InitRemoteSkipCluster(const uint16_t length);
/// Skip the cluster @pos in the clusters list
static inline void SkipRemoteCluster(const uint16_t pos);
/// Return the skip mask
static inline uint16_t GetRemoteSkipMask(void);
/// Check if it is necessary to skip a cluster in @pos
static inline bool IsSkipCluster(uint16_t pos);

/// Function for working with network attempts variable
/// Get current attempt
static inline uint8_t GetNetworkTries(void);
/// Increment current attempt
static inline void IncNetworkTries(void);
/// Clear variable
static inline void ClearNetworkTries(void);


/// Functions for checking which clusters on the remote device we want to bind
/// and checking if the binding already exists in the binding table
/// Check whether our device support some incoming clusters for the passing list.
/// Called during the SC_EZ_DISCOVER state when got a SIMPLE_DESCRIPTOR response
static inline uint8_t CheckSupportedClusters(const uint16_t *incoming_cl_list,
                                          const uint8_t incoming_cl_list_len);

/// Check whether our device already has some bindings in the binding table
/// Called during the SC_EZ_MATCH state for checking whether we already have 
/// all necessary bindings or not
static void MarkDuplicateMatches(void);

/*! Callback for Service Discovery Request */
static void ProcessServiceDiscovery(const EmberAfServiceDiscoveryResult *result);

/*! State Machine Table */
static const SMTask_t sm_transition_table[] = {
  {SC_EZ_STOP, SC_EZEV_START_COMMISSIONING, &StartCommissioning},
  {SC_EZ_START, SC_EZEV_CHECK_NETWORK, &CheckNetwork},
  {SC_EZ_START, SC_EZEV_BCAST_IDENT_QUERY, &BroadcastIdentifyQuery},
  {SC_EZ_START, SC_EZEV_FORM_JOIN_NETWORK, &FormJoinNetwork},
  {SC_EZ_START, SC_EZEV_NETWORK_FAILED, &StopCommissioning},
  {SC_EZ_WAIT_IDENT_RESP, SC_EZEV_TIMEOUT, &StopCommissioning},
  {SC_EZ_DISCOVER, SC_EZEV_CHECK_CLUSTERS, &CheckClusters},
  {SC_EZ_DISCOVER, SC_EZEV_BAD_DISCOVER, &StopCommissioning},
  {SC_EZ_MATCH, SC_EZEV_CHECK_CLUSTERS, &MatchingCheck},
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

/*! Global for storing device's attempts for forming or joining a network
 */
static uint8_t network_access_tries = 0;

/*! \define NETWORK_ACCESS_CONS_TRIES
 *
 * 	Define is for determining how much consecutive tries are allowed
 */
#define NETWORK_ACCESS_CONS_TRIES 3

/*! Global for storing incoming device's info (Short ID, EUI64, endpoint)
 */
MatchDescriptorReq_t incoming_conn;

/*! Global for storing bit mask of which cluster from a remote device must be 
    skipped
 */
RemoteSkipClusters_t skip_mask;

/*! Helper inline function for init DeviceCommissioningClusters struct */
static inline void InitDeviceCommissionInfo(DevCommClusters_t *dcc, const uint8_t ep, const bool is_server,
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
                                     const uint8_t endpoint,
                                     const EmberEUI64 eui64) {
  incoming_conn.source = short_id;
  incoming_conn.source_ep = endpoint;
  MEMCOPY(incoming_conn.source_eui64, eui64, EUI64_SIZE);
}

/*! Helper inline function for setting an incoming connection device's
    clusters list and length of that list
*/
static inline void SetInDevicesClustersInfo(const uint16_t *clusters_list, 
                                     const uint8_t clusters_list_len,
                                     const uint8_t supported_clusters) {
  incoming_conn.source_cl_arr_len = supported_clusters;
  uint8_t supported_cluster_idx = 0;
  
  for (size_t i = 0; i < clusters_list_len; ++i) {
    // if we want to bind to that cluster (appropriate bit in the cluster
    // mask is 1), add it
    if (!IsSkipCluster(i) && supported_cluster_idx < INCOMING_DEVICE_CLUSTERS_LIST_LEN) {
      emberAfDebugPrintln("DEBUG: Supported cluster 0x%X%X", HIGH_BYTE(clusters_list[i]),
                          LOW_BYTE(clusters_list[i]));
      incoming_conn.source_cl_arr[supported_cluster_idx++] = clusters_list[i];
    }
  }
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
  // sanity check that network switched back properly
  EMBER_TEST_ASSERT(status == EMBER_SUCCESS);
}

EmberStatus SimpleCommissioningStart(uint8_t endpoint, 
                                     bool is_server, 
                                     const uint16_t *clusters, 
                                     uint8_t length) {
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
  
  InitDeviceCommissionInfo(&dev_comm_session, endpoint, is_server, clusters, length);
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
    EmberEUI64 eui64;
    EmberStatus status = emberLookupEui64ByNodeId(current_cmd->source, eui64);
    
    if (status != EMBER_SUCCESS) {
      emberAfDebugPrintln("DEBUG: cannot get EUI64 of the remote node");
    }

    SetInConnBaseInfo(current_cmd->source,
                      current_cmd->apsFrame->sourceEndpoint,
                      eui64);
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
  EmberNetworkStatus nw_status = emberNetworkState();
  emberAfDebugPrintln("DEBUG: network state 0x%X", nw_status);
  if (nw_status == EMBER_JOINING_NETWORK ||
  		nw_status == EMBER_LEAVING_NETWORK) {
  	// Try to check again after 5 seconds
  	emberEventControlSetDelayQS(StateMachineEvent, 20);

  	return SC_EZ_START;
  }

  if (nw_status == EMBER_JOINED_NETWORK) {
    SetNextEvent(SC_EZEV_BCAST_IDENT_QUERY);
    // Send Permit Join broadcast to the current network
    // in case of ZED nothing will happen
    // TODO: Make this hadrcoded value as plugin's option
    emberAfPermitJoin(180, TRUE);
  }
  else if (nw_status == EMBER_NO_NETWORK && GetNetworkTries() < NETWORK_ACCESS_CONS_TRIES) {
    // Form or join available network
  	SetNextEvent(SC_EZEV_FORM_JOIN_NETWORK);
  }
  else {
  	SetNextEvent(SC_EZEV_NETWORK_FAILED);
  }

  // if the device is in the network continue commissioning
  // by sending Identify Query
  emberEventControlSetActive(StateMachineEvent);
  
  return SC_EZ_START;
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
  emberAfDebugPrintln("DEBUG: Unknown operation requested on stage 0x%X", GetNextState());
  
  return SC_EZ_STOP;
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
    // init clusters skip mask length for further using
    InitRemoteSkipCluster(inc_clusters_arr_len);
    // check how much clusters our device wants to bind to
    uint8_t supported_clusters = CheckSupportedClusters(inc_clusters_arr, 
                                                        inc_clusters_arr_len);
    
    emberAfDebugPrintln("DEBUG: Supported clusters %d", supported_clusters);
    if (supported_clusters == 0) {
      // we should not do anything with that
      SetNextEvent(SC_EZEV_BINDING_DONE);
      SetNextState(SC_EZ_BIND);
    }
    else {
      // update our incoming device structure with information about clusters
      SetInDevicesClustersInfo(inc_clusters_arr, inc_clusters_arr_len,
                               supported_clusters);
      // Now we have all information about responded device's clusters
      // Start matching procedure for checking how much of them fit for our
      // device
      SetNextEvent(SC_EZEV_CHECK_CLUSTERS);
      SetNextState(SC_EZ_MATCH);
    }
  }
  else {
    SetNextEvent(SC_EZEV_BAD_DISCOVER);
  }
  
  emberEventControlSetActive(StateMachineEvent);
}

static inline uint8_t FindUnusedBindingIndex(void) {
  EmberBindingTableEntry entry = {0};
  
  for (size_t i = 0; i < emberBindingTableSize; ++i) {
    if (emberGetBinding(i, &entry) != EMBER_SUCCESS) {
      // something bad happened with Binding Table
      emberAfDebugPrintln("DEBUG: error: cannot get the binding entry");
      return EMBER_APPLICATION_ERROR_0;
    }
    
    if (entry.type == EMBER_UNUSED_BINDING) {
      return i;
    }
  }
  
  // Binding table is full
  return EMBER_APPLICATION_ERROR_1;
}

static inline void InitBindingTableEntry(const EmberEUI64 remote_eui64,
                                         const uint16_t cluster_id, 
                                         EmberBindingTableEntry *entry) {
  entry->type = EMBER_UNICAST_BINDING;
  entry->local = dev_comm_session.ep;
  entry->remote = incoming_conn.source_ep;
  entry->clusterId = cluster_id;
  MEMCOPY(entry->identifier, remote_eui64, EUI64_SIZE);
}

static CommissioningState_t SetBinding(void) {
  emberAfDebugPrintln("DEBUG: Set Binding");
  EmberStatus status = EMBER_SUCCESS;
  // here we add bindings to the binding table
  for (size_t i = 0; i < incoming_conn.source_cl_arr_len; ++i) {
    if (!IsSkipCluster(i)) {
      uint8_t bindex = FindUnusedBindingIndex();
      
      if (bindex == EMBER_APPLICATION_ERROR_0 ||
          bindex == EMBER_APPLICATION_ERROR_1) {
        // error during finding an available binding table index for write to
        SetNextEvent(SC_EZEV_UNKNOWN);
        break;
      }

      EmberBindingTableEntry new_binding;
      InitBindingTableEntry(incoming_conn.source_eui64, 
                            incoming_conn.source_cl_arr[i],
                            &new_binding);
      status = emberSetBinding(bindex, &new_binding);
      if (status == EMBER_SUCCESS) {
        // Set up the remote short ID for binding for avoiding ZDO broadcast
        emberSetBindingRemoteNodeId(bindex, incoming_conn.source);
      }
      
      // DEBUG
      emberGetBinding(bindex, &new_binding);
      emberAfDebugPrintln("DEBUG: remote ep 0x%X", new_binding.remote);
      emberAfDebugPrintln("DEBUG: cluster id 0x%X%X", HIGH_BYTE(new_binding.clusterId),
                          LOW_BYTE(new_binding.clusterId));
    }
  }
  
  // all bindings successfully added
  SetNextEvent(SC_EZEV_BINDING_DONE);
  emberEventControlSetActive(StateMachineEvent);
  
  return SC_EZ_BIND;
}

static CommissioningState_t StopCommissioning(void) {
  emberAfDebugPrintln("DEBUG: Stop commissioning");
  emberAfDebugPrintln("Current state is 0x%X", GetNextState());  
  SetNextEvent(SC_EZEV_UNKNOWN);
  // clean up globals
  ClearNetworkTries();
  
  return SC_EZ_UNKNOWN;
}

static void MarkDuplicateMatches(void) {
  // Check if we already have any from requested clusters from a remote
  EmberBindingTableEntry entry = {0};
  // run through the incoming device's clusters list and check if 
  // we already have any binding entries
  for (size_t i = 0; i < incoming_conn.source_cl_arr_len; ++i) {
    for (size_t j = 0; j < emberBindingTableSize; ++j) {
      if (emberGetBinding(j, &entry) != EMBER_SUCCESS) {
        break;
      }
      // if a binding entry not marked as unused and
      // current info are the same as in a request
      if (entry.type != EMBER_UNUSED_BINDING &&
          entry.local == dev_comm_session.ep &&
          entry.clusterId == incoming_conn.source_cl_arr[i] &&
          entry.remote == incoming_conn.source_ep &&
          MEMCOMPARE(entry.identifier, incoming_conn.source_eui64, EUI64_SIZE) == 0) 
      {
        SkipRemoteCluster(i);
        break;
      }
    }
  }
}

static CommissioningState_t MatchingCheck(void) {
  emberAfDebugPrintln("DEBUG: Matching Check");
  // init clusters skip mask length for checking for duplicates in the binding
  // table
  InitRemoteSkipCluster(incoming_conn.source_cl_arr_len);
  // check the supported clusters list for existence in the binding table
  MarkDuplicateMatches();
  // nothing to do if we unmarked all clusters
  if (GetRemoteSkipMask() == 0) {
    SetNextEvent(SC_EZEV_BINDING_DONE);
  }
  else {
    SetNextEvent(SC_EZEV_BIND);
  }

  emberAfDebugPrintln("DEBUG: Supported clusters mask 0x%X", skip_mask.skip_clusters);
  emberEventControlSetActive(StateMachineEvent);
  
  return SC_EZ_BIND;
}

static inline void InitRemoteSkipCluster(const uint16_t length) {
  // as init we don't want to skip anything
  skip_mask.len = length;
  skip_mask.skip_clusters = (1 << skip_mask.len) - 1;
}

static inline void SkipRemoteCluster(const uint16_t pos) {
  EMBER_TEST_ASSERT(pos < skip_mask->len);
  // just clean the appropriate bit
  skip_mask.skip_clusters &= ~(1 << pos);
}

static inline uint16_t GetRemoteSkipMask(void) {
  return skip_mask.skip_clusters;
}
        
static inline bool IsSkipCluster(uint16_t pos) {
  return !(BIT(pos) & skip_mask.skip_clusters);
}

static inline uint8_t CheckSupportedClusters(const uint16_t *incoming_cl_list,
                                          const uint8_t incoming_cl_list_len) {
  bool unused = true;
  uint8_t supported_clusters_cnt = 0;
  
  for (size_t i = 0; i < incoming_cl_list_len; ++i) {
    // just run through the current device's cluster list and if 
    // that incoming cluster exists on our device then don't exclude it
    for (size_t j = 0; j < dev_comm_session.clusters_arr_len; ++j) {
      if (incoming_cl_list[i] == dev_comm_session.clusters[j]) {
        // our device support it
        ++supported_clusters_cnt;
        unused = false;
        break;
      }
    }
    
    if (unused) {
      SkipRemoteCluster(i);
    }
    
    unused = true;
  }
  
  return supported_clusters_cnt;
}

static CommissioningState_t FormJoinNetwork(void) {
	emberAfDebugPrintln("DEBUG: Form/Join network");
  // Form or join depends on the device type
  // Coordinator: form a network
  // Router/ZED/SED/MED: find a network
  EmberStatus status = EMBER_SUCCESS;

  if (emAfCurrentZigbeeProNetwork->nodeType == EMBER_COORDINATOR) {
  	status = emberAfFindUnusedPanIdAndForm();
  }
  else {
  	status = emberAfStartSearchForJoinableNetwork();
  }

  if (status != EMBER_SUCCESS) {
  	SetNextEvent(SC_EZEV_UNKNOWN);
  }
  else {
  	SetNextEvent(SC_EZEV_CHECK_NETWORK);
  }

  IncNetworkTries();
  // run state machine again after 10 seconds
  emberEventControlSetDelayQS(StateMachineEvent, 40);

  return SC_EZ_START;
}

/// Get current attempt
static inline uint8_t GetNetworkTries(void) {
	return network_access_tries;
}
/// Increment current attempt
static inline void IncNetworkTries(void) {
	++network_access_tries;
}
/// Clear variable
static inline void ClearNetworkTries(void) {
	network_access_tries = 0;
}
