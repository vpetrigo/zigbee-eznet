#include "simple-commissioning-initiator-buffer.h"

/// \typedef QUEUE_SIZE
///
/// Handy renaming of the long name EMBER_AF_PLUGIN_SIMPLE_COMMISSIONING_INITIATOR_REMOTES_QUEUE
#define QUEUE_SIZE EMBER_AF_PLUGIN_SIMPLE_COMMISSIONING_INITIATOR_REMOTES_QUEUE

MatchDescriptorReq_t data[QUEUE_SIZE];

#define RING_BUFFER_ERROR 255

// Internal container for remote devices' Match Descriptors
// Simple Ring Buffer
typedef struct RingBuffer {
  MatchDescriptorReq_t *buffer;
  uint8_t begin;
  uint8_t end;
  uint8_t size;
  uint8_t capacity;
} RingBuffer_t;

// Remote Devices queue
typedef struct MatchDescriptorQueue {
  RingBuffer_t internal_data;
} MatchDescriptorQueue_t;

// Global queue
MatchDescriptorQueue_t devices_queue;

// Queue private interface
static inline void InitQueueInternalData(MatchDescriptorQueue_t *queue);
static inline bool QueueIsFull(MatchDescriptorQueue_t *queue);

// Ring Buffer interface
static inline void RingBufferInit(RingBuffer_t *buf);
static inline bool RingBufferIsFull(RingBuffer_t *buf);
static inline bool RingBufferIsEmpty(RingBuffer_t *buf);
static inline uint8_t RingBufferSize(RingBuffer_t *buf);
static inline uint8_t RingBufferPush(RingBuffer_t *buf, const MatchDescriptorReq_t * const data);
static inline uint8_t RingBufferPopFront(RingBuffer_t *buf);
static inline void *RingBufferGet(RingBuffer_t *buf);

static inline void RingBufferInit(RingBuffer_t *buf) {
	buf->buffer = data;
	buf->begin = 0;
	buf->end = 0;
	buf->size = 0;
	buf->capacity = QUEUE_SIZE;
}
static inline bool RingBufferIsFull(RingBuffer_t *buf) {
  return (buf->size == buf->capacity);
}

static inline bool RingBufferIsEmpty(RingBuffer_t *buf) {
  return (buf->size == 0);
}

static inline uint8_t RingBufferSize(RingBuffer_t *buf) {
  return buf->size;
}

static inline uint8_t RingBufferPush(RingBuffer_t *buf, const MatchDescriptorReq_t * const data) {
  // Cicular buffer is full
  if (RingBufferIsFull(buf)) {
      // quit with an error
      return RING_BUFFER_ERROR;
  }

  ++buf->size;
  buf->buffer[buf->end] = *data;
  buf->end = (buf->end + 1) % buf->capacity;
  
  return 0;
}
 
static inline uint8_t RingBufferPopFront(RingBuffer_t *buf) {
  // if the head isn't ahead of the tail, we don't have any characters
  if (RingBufferIsEmpty(buf)) {
      return RING_BUFFER_ERROR;
  }
  
  --buf->size;
  buf->begin = (buf->begin + 1) % buf->capacity;

  return 0;
}

static inline void *RingBufferGet(RingBuffer_t *buf) {
  if (RingBufferIsEmpty(buf)) {
    // quit with an error
    return NULL;
  }

  return &buf->buffer[buf->begin];
}

static inline void InitQueueInternalData(MatchDescriptorQueue_t *queue) {
	RingBufferInit(&queue->internal_data);
}

static inline bool QueueIsFull(MatchDescriptorQueue_t *queue) {
	return RingBufferIsFull(&queue->internal_data);
}

// Public interface implementation
void InitQueue(void) {
  InitQueueInternalData(&devices_queue);
}

bool AddInDeviceDescriptor(const EmberNodeId short_id, const uint8_t endpoint) {
  if (!QueueIsFull(&devices_queue)) {
    MatchDescriptorReq_t in_conn = {
      .source = short_id,
      .source_ep = endpoint
    };
    
    uint8_t status = RingBufferPush(&devices_queue.internal_data, &in_conn);
    return (status != RING_BUFFER_ERROR) ? true : false;
  }
  
  return false;
}

MatchDescriptorReq_t *GetTopInDeviceDescriptor(void) {
  return (MatchDescriptorReq_t *) RingBufferGet(&devices_queue.internal_data);
}

void PopInDeviceDescriptor(void) {
  RingBufferPopFront(&devices_queue.internal_data);
}

uint8_t GetQueueSize(void) {
  return RingBufferSize(&devices_queue.internal_data);
}
