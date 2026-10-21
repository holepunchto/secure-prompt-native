#include <stddef.h>
#include <stdint.h>

#include "editor.h"

enum {
  SECURE_PROMPT_ESC_NONE = 0,
  SECURE_PROMPT_ESC_START, // after ESC
  SECURE_PROMPT_ESC_CSI,   // inside ESC [
  SECURE_PROMPT_ESC_SS3,   // inside ESC O
};

void
secure_prompt_editor_init(secure_prompt_editor_t *editor, uint8_t *line, size_t capacity) {
  editor->line = line;
  editor->capacity = capacity;
  editor->len = 0;
  editor->esc = SECURE_PROMPT_ESC_NONE;
}

secure_prompt_editor_status_t
secure_prompt_editor_feed(secure_prompt_editor_t *editor, const uint8_t *data, size_t len) {
  secure_prompt_editor_status_t status = SECURE_PROMPT_EDITOR_MORE;

  for (size_t i = 0; i < len && status == SECURE_PROMPT_EDITOR_MORE; i++) {
    uint8_t b = data[i];

    switch (editor->esc) {
    case SECURE_PROMPT_ESC_START:
      editor->esc = b == '[' ? SECURE_PROMPT_ESC_CSI : b == 'O' ? SECURE_PROMPT_ESC_SS3 : SECURE_PROMPT_ESC_NONE;
      continue;

    case SECURE_PROMPT_ESC_CSI:
      if (b >= 0x40 && b <= 0x7e) editor->esc = SECURE_PROMPT_ESC_NONE;
      continue;

    case SECURE_PROMPT_ESC_SS3:
      editor->esc = SECURE_PROMPT_ESC_NONE;
      continue;
    }

    switch (b) {
    case 0x1b:
      editor->esc = SECURE_PROMPT_ESC_START;
      break;

    case 0x7f:
    case '\b':
      // Drop a whole UTF-8 character: continuation bytes, then the lead byte
      while (editor->len > 0) {
        if ((editor->line[--editor->len] & 0xc0) != 0x80) break;
      }
      break;

    case '\r':
    case '\n':
      status = SECURE_PROMPT_EDITOR_DONE;
      break;

    case 0x03: // Ctrl-C
    case 0x04: // Ctrl-D
      status = SECURE_PROMPT_EDITOR_CANCEL;
      break;

    default:
      if (b < 0x20) break;

      if (editor->len == editor->capacity) {
        status = SECURE_PROMPT_EDITOR_OVERFLOW;
        break;
      }

      editor->line[editor->len++] = b;
    }
  }

  // A lone ESC keypress arrives in a read of its own
  if (editor->esc == SECURE_PROMPT_ESC_START) editor->esc = SECURE_PROMPT_ESC_NONE;

  return status;
}
