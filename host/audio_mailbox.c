#include "audio_mailbox.h"

#include <string.h>

#define UNREAD 4u

static unsigned char *slot(audio_mailbox *mailbox, unsigned index) {
  return mailbox->slots + index * mailbox->size;
}

void audio_mailbox_init(audio_mailbox *mailbox, void *slots, size_t size, const void *first) {
  mailbox->slots = slots;
  mailbox->size = size;
  for (unsigned i = 0; i < 3; ++i) memcpy(slot(mailbox, i), first, size);
  mailbox->writing = 0;
  mailbox->reading = 2;
  atomic_init(&mailbox->shared, 1u);
}

void audio_mailbox_publish(audio_mailbox *mailbox, const void *value) {
  memcpy(slot(mailbox, mailbox->writing), value, mailbox->size);
  /* Release publishes the slot contents. Acquire orders the reader's last use of
     the returned slot before the next write to it. */
  unsigned previous = atomic_exchange_explicit(&mailbox->shared, mailbox->writing | UNREAD,
                                               memory_order_acq_rel);
  mailbox->writing = previous & ~UNREAD;
}

const void *audio_mailbox_take(audio_mailbox *mailbox) {
  if (!(atomic_load_explicit(&mailbox->shared, memory_order_relaxed) & UNREAD)) return NULL;
  unsigned previous = atomic_exchange_explicit(&mailbox->shared, mailbox->reading,
                                               memory_order_acq_rel);
  mailbox->reading = previous & ~UNREAD;
  return slot(mailbox, mailbox->reading);
}
