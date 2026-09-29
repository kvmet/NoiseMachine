#ifndef AUDIO_MAILBOX_H
#define AUDIO_MAILBOX_H

#include <stdatomic.h>
#include <stdint.h>

#include "noise_core.h"

/* Everything the control thread asks of the audio thread, sent whole. */
typedef struct audio_request {
  noise_config config;
  uint32_t seed;
  uint32_t reset_count;  /* The audio thread reinitializes when this changes. */
  uint32_t strike_count; /* The audio thread starts `strike` when this changes. */
  thunder_strike strike;
} audio_request;

/* Triple buffer with one writer thread and one reader thread. The reader always
   sees a whole request, and neither side waits for the other. */
typedef struct audio_mailbox {
  audio_request slot[3];
  atomic_uint shared; /* Slot index between the threads, with a bit set until read. */
  unsigned writing;   /* Owned by the writer. */
  unsigned reading;   /* Owned by the reader. */
} audio_mailbox;

void audio_mailbox_init(audio_mailbox *mailbox, const audio_request *first);
/* Writer thread only. */
void audio_mailbox_publish(audio_mailbox *mailbox, const audio_request *request);
/* Reader thread only. Returns the newest unread request, valid until the next
   take, or NULL when nothing was published since the last take. */
const audio_request *audio_mailbox_take(audio_mailbox *mailbox);

#endif
