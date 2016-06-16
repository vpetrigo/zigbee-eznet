#ifndef SIMPLE_COMMISSIONING_TYPEDEFS_H
#define SIMPLE_COMMISSIONING_TYPEDEFS_H

/*! \typedef struct DevicesCommissioningClusters
    \brief Device's clusters for commissioning

    Storage about device's endpoint and clusters
    for current commissioning call
*/

typedef struct DeviceCommissioningClusters {
  uint16_t *clusters;
  uint8_t ep;
  uint8_t clusters_arr_len;
  uint8_t network_index;
  bool is_server
} DevCommClusters_t;

/*! \typedef struct MatchDescriptorReq
    \brief Match Descriptor Request structure

    When a device that sent Identify Query gets Identify
    Query Responses, we need information listed below 
    for further Binding state
*/
typedef struct MatchDescriptorReq {
  /// Node short ID
  EmberNodeId source;
  /// Node EUI64 (uint8_t[EUI64_SIZE] type)
  EmberEUI64 source_eui64;
  /// Node endpoint
  uint8_t source_ep;
} MatchDescriptorReq_t;

/*! \typedef enum CommissioningStates
    \brief Commissioning States

    Commissioning States for using with the plugin's internal
    state machine
*/
typedef enum CommissioningStates {
  SC_EZ_STOP = 0,         //!< Commissioning inactive
  SC_EZ_START,            //!< Commissioning start phase
  SC_EZ_WAIT_IDENT_RESP,  //!< Awaiting for responses
  SC_EZ_DISCOVER,         //!< Discover clusters
  SC_EZ_MATCH,            //!< Matching state
  SC_EZ_BIND,             //!< Cluster binding
  SC_EZ_UNKNOWN = 255     //!< Error
} CommissioningState_t;

/*! \typedef enum CommissioningEvents
    \brief Commissioning Events
    
    Commissioning events describe what state machine
    have to do in different cases
*/
typedef enum CommissioningEvents {
  SC_EZEV_START_COMMISSIONING = 0,
  SC_EZEV_CHECK_NETWORK,
  SC_EZEV_BCAST_IDENT_QUERY,
  SC_EZEV_GOT_RESP,
  SC_EZEV_TIMEOUT,
  SC_EZEV_CHECK_CLUSTERS,
  SC_EZEV_BAD_DISCOVER,
  SC_EZEV_NOT_MATCHED,
  SC_EZEV_BIND,
  SC_EZEV_BINDING_DONE,
  SC_EZEV_UNKNOWN = 255
} CommissioningEvent_t;

/*! \typedef struct StateMachineTask
    \brief State Machine Task definition
    
    Every task describes particular state, event and
    handler that has to be called
*/
typedef struct StateMachineTask {
  CommissioningState_t state;
  CommissioningEvent_t event;
  CommissioningState_t (*handler)(void);
} SMTask_t;

/*! \typedef struct StateMachineNextState
    
    Typedef for storing the next state for the state machine
*/
typedef struct StateMachineNextState {
  CommissioningState_t next_state;
  CommissioningEvent_t next_event;
} SMNext_t;

/*! Initial value for the state machine
*/
static const SMNext_t INIT_VALUE = {
  SC_EZ_UNKNOWN,
  SC_EZEV_UNKNOWN
};

#endif // SIMPLE_COMMISSIONING_TYPEDEFS_H
