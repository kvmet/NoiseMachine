#ifndef AUDIO_MAILBOX_H
#define AUDIO_MAILBOX_H

#include <stdatomic.h>
#include <stddef.h>

/* Triple buffer of fixed-size values with one writer thread and one reader thread.
   The reader always sees a whole value, and neither side waits for the other. */
typedef struct audio_mailbox {
  unsigned char *slots; /* Three values of size bytes, owned by the caller. */
  size_t size;
  atomic_uint shared; /* Slot index between the threads, with a bit set until read. */
  unsigned writing;   /* Owned by the writer. */
  unsigned reading;   /* Owned by the reader. */
} audio_mailbox;

/* slots holds 3 * size bytes; every slot starts as a copy of first. */
void audio_mailbox_init(audio_mailbox *mailbox, void *slots, size_t size, const void *first);
/* Writer thread only. Copies size bytes from value. */
void audio_mailbox_publish(audio_mailbox *mailbox, const void *value);
/* Reader thread only. Returns the newest unread value, valid until the next take,
   or NULL when nothing was published since the last take. */
const void *audio_mailbox_take(audio_mailbox *mailbox);

#endif
