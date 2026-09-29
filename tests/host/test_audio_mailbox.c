#include "audio_mailbox.h"

#include <assert.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define PUBLISHES 200000u

/* Large enough that a torn copy is likely to show. */
typedef struct audio_request {
  uint32_t seed;
  unsigned char payload[4096];
} audio_request;

static audio_request marked(uint32_t mark) {
  audio_request request;
  memset(&request, (int)(mark & 0xffu), sizeof(request));
  request.seed = mark;
  return request;
}

/* Every byte after seed must repeat the low byte of seed. */
static int whole(const audio_request *request) {
  const unsigned char *bytes = (const unsigned char *)request;
  unsigned char low = (unsigned char)(request->seed & 0xffu);
  size_t seed_end = offsetof(audio_request, seed) + sizeof(request->seed);
  for (size_t i = 0; i < sizeof(*request); ++i) {
    if ((i < offsetof(audio_request, seed) || i >= seed_end) && bytes[i] != low) return 0;
  }
  return 1;
}

static void test_single_thread(void) {
  static audio_mailbox mailbox;
  static audio_request slots[3];
  audio_request first = marked(1);
  audio_mailbox_init(&mailbox, slots, sizeof(first), &first);
  assert(audio_mailbox_take(&mailbox) == NULL);

  audio_request second = marked(2);
  audio_mailbox_publish(&mailbox, &second);
  const audio_request *taken = audio_mailbox_take(&mailbox);
  assert(taken && taken->seed == 2 && whole(taken));
  assert(audio_mailbox_take(&mailbox) == NULL);

  for (uint32_t mark = 3; mark <= 5; ++mark) {
    audio_request request = marked(mark);
    audio_mailbox_publish(&mailbox, &request);
  }
  taken = audio_mailbox_take(&mailbox);
  assert(taken && taken->seed == 5 && whole(taken));
  assert(audio_mailbox_take(&mailbox) == NULL);
}

static audio_mailbox shared_mailbox;
static audio_request shared_slots[3];

static void *write_all(void *unused) {
  (void)unused;
  for (uint32_t mark = 1; mark <= PUBLISHES; ++mark) {
    audio_request request = marked(mark);
    audio_mailbox_publish(&shared_mailbox, &request);
  }
  return NULL;
}

static void test_two_threads(void) {
  audio_request first = marked(0);
  audio_mailbox_init(&shared_mailbox, shared_slots, sizeof(first), &first);
  pthread_t writer;
  assert(pthread_create(&writer, NULL, write_all, NULL) == 0);
  uint32_t last = 0;
  while (last < PUBLISHES) {
    const audio_request *taken = audio_mailbox_take(&shared_mailbox);
    if (!taken) continue;
    assert(whole(taken));
    assert(taken->seed > last);
    last = taken->seed;
  }
  assert(pthread_join(writer, NULL) == 0);
}

void run_audio_mailbox_tests(void) {
  test_single_thread();
  test_two_threads();
}
