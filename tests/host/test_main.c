#include <stdio.h>

void run_audio_mailbox_tests(void);
void run_gui_controls_tests(void);

int main(void) {
  run_audio_mailbox_tests();
  run_gui_controls_tests();
  printf("host checks passed\n");
  return 0;
}
