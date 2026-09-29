#include "audio_mailbox.h"

#define UNREAD 4u

void audio_mailbox_init(audio_mailbox *mailbox, const audio_request *first) {
  for (unsigned i = 0; i < 3; ++i) mailbox->slot[i] = *first;
  mailbox->writing = 0;
  mailbox->reading = 2;
  atomic_init(&mailbox->shared, 1u);
}

void audio_mailbox_publish(audio_mailbox *mailbox, const audio_request *request) {
  mailbox->slot[mailbox->writing] = *request;
  /* Release publishes the slot contents. Acquire orders the reader's last use of
     the returned slot before the next write to it. */
  unsigned previous = atomic_exchange_explicit(&mailbox->shared, mailbox->writing | UNREAD,
                                               memory_order_acq_rel);
  mailbox->writing = previous & ~UNREAD;
}

const audio_request *audio_mailbox_take(audio_mailbox *mailbox) {
  if (!(atomic_load_explicit(&mailbox->shared, memory_order_relaxed) & UNREAD)) return NULL;
  unsigned previous = atomic_exchange_explicit(&mailbox->shared, mailbox->reading,
                                               memory_order_acq_rel);
  mailbox->reading = previous & ~UNREAD;
  return &mailbox->slot[mailbox->reading];
}
