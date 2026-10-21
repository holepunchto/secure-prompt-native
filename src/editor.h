#ifndef SECURE_PROMPT_EDITOR_H
#define SECURE_PROMPT_EDITOR_H

#include <stddef.h>
#include <stdint.h>

typedef struct secure_prompt_editor_s secure_prompt_editor_t;

typedef enum {
  SECURE_PROMPT_EDITOR_MORE = 0,
  SECURE_PROMPT_EDITOR_DONE,
  SECURE_PROMPT_EDITOR_CANCEL,
  SECURE_PROMPT_EDITOR_OVERFLOW,
} secure_prompt_editor_status_t;

struct secure_prompt_editor_s {
  uint8_t *line;
  size_t capacity;
  size_t len;
  int esc;
};

void
secure_prompt_editor_init(secure_prompt_editor_t *editor, uint8_t *line, size_t capacity);

/**
 * Feed one read of raw terminal input into the line. Enter submits, Ctrl-C and
 * Ctrl-D cancel, backspace removes a whole UTF-8 character, and escape
 * sequences and other control characters are ignored. The line must be
 * writable for the duration of the call.
 */
secure_prompt_editor_status_t
secure_prompt_editor_feed(secure_prompt_editor_t *editor, const uint8_t *data, size_t len);

#endif // SECURE_PROMPT_EDITOR_H
