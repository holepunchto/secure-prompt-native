#include <assert.h>
#include <bare.h>
#include <errno.h>
#include <js.h>
#include <mprotect.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "src/editor.h"

#define SECURE_PROMPT_READ_SIZE 4096

typedef struct {
  uv_tty_t tty;

  js_env_t *env;
  js_deferred_t *deferred;

#ifdef _WIN32
  HANDLE output;
#else
  int fd;
#endif

  // libuv reads straight into this, so it stays writable while the prompt is
  // active and is wiped after every read
  mprotect_t input;

  // Only writable while a read is fed into the editor
  mprotect_t line;

  secure_prompt_editor_t editor;

  // How the prompt ended, settled once the handle has closed
  secure_prompt_editor_status_t status;
  int uv_err;
} secure_prompt_native_t;

// A completed line, handed to JavaScript until it is copied out with `take()`
typedef struct {
  mprotect_t line;
  size_t len;
} secure_prompt_native_result_t;

// libmprotect returns negative CRT errno values, which are not libuv error
// codes on Windows
static int
secure_prompt_native__uv_error(int err) {
  switch (-err) {
  case EINVAL:
    return UV_EINVAL;
  case ENOMEM:
    return UV_ENOMEM;
  case ENOSYS:
    return UV_ENOSYS;
  case EPERM:
    return UV_EPERM;
  case EACCES:
    return UV_EACCES;
  default:
    return UV_EIO;
  }
}

static void
secure_prompt_native__wipe(void *data, size_t len) {
  volatile uint8_t *p = data;
  while (len--) *p++ = 0;
}

static void
secure_prompt_native__write(secure_prompt_native_t *prompt, const char *data, size_t len) {
#ifdef _WIN32
  DWORD written;
  if (prompt->output != INVALID_HANDLE_VALUE) WriteFile(prompt->output, data, (DWORD) len, &written, NULL);
#else
  while (len > 0) {
    ssize_t n = write(prompt->fd, data, len);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) continue;
    if (n <= 0) return;
    data += n;
    len -= n;
  }
#endif
}

static int
secure_prompt_native__open(secure_prompt_native_t *prompt, uv_file *result) {
#ifdef _WIN32
  HANDLE input = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (input == INVALID_HANDLE_VALUE) return UV_ENXIO;

  int fd = _open_osfhandle((intptr_t) input, _O_RDONLY);
  if (fd < 0) {
    CloseHandle(input);
    return UV_ENXIO;
  }

  prompt->output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

  *result = fd;
#else
  int fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) return uv_translate_sys_error(errno);

  prompt->fd = fd;

  *result = fd;
#endif

  return 0;
}

static void
secure_prompt_native__on_result_finalize(js_env_t *env, void *data, void *finalize_hint) {
  secure_prompt_native_result_t *result = data;

  mprotect_free(&result->line);

  free(result);
}

static js_value_t *
secure_prompt_native__error(js_env_t *env, const char *code, const char *message) {
  int err;

  js_value_t *js_code;
  err = js_create_string_utf8(env, (const utf8_t *) code, -1, &js_code);
  assert(err == 0);

  js_value_t *js_message;
  err = js_create_string_utf8(env, (const utf8_t *) message, -1, &js_message);
  assert(err == 0);

  js_value_t *error;
  err = js_create_error(env, js_code, js_message, &error);
  assert(err == 0);

  return error;
}

static void
secure_prompt_native__settle(secure_prompt_native_t *prompt) {
  int err;

  js_env_t *env = prompt->env;

  js_handle_scope_t *scope;
  err = js_open_handle_scope(env, &scope);
  assert(err == 0);

  js_value_t *error = NULL;

  if (prompt->uv_err < 0) {
    error = prompt->uv_err == UV_EOF
              ? secure_prompt_native__error(env, "EOF", "Unexpected end of input")
              : secure_prompt_native__error(env, uv_err_name(prompt->uv_err), uv_strerror(prompt->uv_err));
  } else if (prompt->status == SECURE_PROMPT_EDITOR_CANCEL) {
    error = secure_prompt_native__error(env, "CANCELLED", "Prompt cancelled");
  } else if (prompt->status == SECURE_PROMPT_EDITOR_OVERFLOW) {
    error = secure_prompt_native__error(env, "TOO_LONG", "Input too long");
  } else if (prompt->editor.len == 0) {
    error = secure_prompt_native__error(env, "NO_INPUT", "No input");
  }

  secure_prompt_native_result_t *result = NULL;

  if (error == NULL) {
    result = malloc(sizeof(secure_prompt_native_result_t));

    if (result == NULL) {
      error = secure_prompt_native__error(env, "ENOMEM", "Out of memory");
    } else {
      // Move the line into the result so it outlives the prompt
      result->line = prompt->line;
      result->len = prompt->editor.len;

      memset(&prompt->line, 0, sizeof(mprotect_t));
    }
  }

  if (error) {
    err = js_reject_deferred(env, prompt->deferred, error);
    assert(err == 0);
  } else {
    js_value_t *external;
    err = js_create_external(env, result, secure_prompt_native__on_result_finalize, NULL, &external);
    assert(err == 0);

    js_value_t *len;
    err = js_create_int64(env, (int64_t) result->len, &len);
    assert(err == 0);

    js_value_t *resolution;
    err = js_create_array_with_length(env, 2, &resolution);
    assert(err == 0);

    err = js_set_element(env, resolution, 0, external);
    assert(err == 0);

    err = js_set_element(env, resolution, 1, len);
    assert(err == 0);

    err = js_resolve_deferred(env, prompt->deferred, resolution);
    assert(err == 0);
  }

  err = js_close_handle_scope(env, scope);
  assert(err == 0);
}

static void
secure_prompt_native__on_close(uv_handle_t *handle) {
  secure_prompt_native_t *prompt = (secure_prompt_native_t *) handle;

  // Closing a tty handle can restore the terminal settings libuv saved when it
  // first entered raw mode. Settling only now means a following prompt cannot
  // enter raw mode before that happens and then be put back into echo mode.
  if (prompt->deferred) secure_prompt_native__settle(prompt);

  mprotect_free(&prompt->line);

#ifdef _WIN32
  if (prompt->output != INVALID_HANDLE_VALUE) CloseHandle(prompt->output);
#endif

  free(prompt);
}

static void
secure_prompt_native__finish(secure_prompt_native_t *prompt, secure_prompt_editor_status_t status, int uv_err) {
  uv_read_stop((uv_stream_t *) &prompt->tty);

  uv_tty_set_mode(&prompt->tty, UV_TTY_MODE_NORMAL);

  // Echo is off, so the Enter that ended the line did not move the cursor
  secure_prompt_native__write(prompt, "\n", 1);

  mprotect_free(&prompt->input);

  prompt->status = status;
  prompt->uv_err = uv_err;

  uv_close((uv_handle_t *) &prompt->tty, secure_prompt_native__on_close);
}

static void
secure_prompt_native__on_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  secure_prompt_native_t *prompt = (secure_prompt_native_t *) handle;

  *buf = uv_buf_init((char *) prompt->input.data, (unsigned int) prompt->input.len);
}

static void
secure_prompt_native__on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  secure_prompt_native_t *prompt = (secure_prompt_native_t *) stream;

  if (nread == 0) return;

  if (nread < 0) {
    secure_prompt_native__finish(prompt, SECURE_PROMPT_EDITOR_MORE, (int) nread);
    return;
  }

  int err = mprotect_readwrite(&prompt->line);

  secure_prompt_editor_status_t status = SECURE_PROMPT_EDITOR_MORE;

  if (err == 0) {
    status = secure_prompt_editor_feed(&prompt->editor, prompt->input.data, (size_t) nread);

    mprotect_noaccess(&prompt->line);
  }

  secure_prompt_native__wipe(prompt->input.data, (size_t) nread);

  if (err < 0) secure_prompt_native__finish(prompt, status, secure_prompt_native__uv_error(err));
  else if (status != SECURE_PROMPT_EDITOR_MORE) secure_prompt_native__finish(prompt, status, 0);
}

static void
secure_prompt_native__throw(js_env_t *env, int uv_err) {
  js_throw_error(env, uv_err_name(uv_err), uv_strerror(uv_err));
}

static js_value_t *
secure_prompt_native_prompt(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc == 2);

  size_t message_len;
  err = js_get_value_string_utf8(env, argv[0], NULL, 0, &message_len);
  assert(err == 0);

  int64_t capacity;
  err = js_get_value_int64(env, argv[1], &capacity);
  assert(err == 0);

  assert(capacity > 0);

  uv_loop_t *loop;
  err = js_get_env_loop(env, &loop);
  assert(err == 0);

  secure_prompt_native_t *prompt = calloc(1, sizeof(secure_prompt_native_t));

  if (prompt == NULL) {
    secure_prompt_native__throw(env, UV_ENOMEM);
    return NULL;
  }

  prompt->env = env;

#ifdef _WIN32
  prompt->output = INVALID_HANDLE_VALUE;
#else
  prompt->fd = -1;
#endif

  err = mprotect_alloc(&prompt->input, SECURE_PROMPT_READ_SIZE);
  if (err == 0) err = mprotect_alloc(&prompt->line, (size_t) capacity);

  if (err < 0) {
    mprotect_free(&prompt->input);
    free(prompt);
    secure_prompt_native__throw(env, secure_prompt_native__uv_error(err));
    return NULL;
  }

  // The line stays inaccessible except while a read is fed into the editor
  secure_prompt_editor_init(&prompt->editor, prompt->line.data, prompt->line.len);

  uv_file fd;
  err = secure_prompt_native__open(prompt, &fd);

  if (err == 0) {
    utf8_t *message = malloc(message_len + 1);

    if (message) {
      err = js_get_value_string_utf8(env, argv[0], message, message_len + 1, NULL);
      assert(err == 0);

      // Written before libuv makes the descriptor non-blocking
      secure_prompt_native__write(prompt, (const char *) message, message_len);

      free(message);
    }

    err = uv_tty_init(loop, &prompt->tty, fd, 1);
  }

  if (err < 0) {
#ifdef _WIN32
    if (prompt->output != INVALID_HANDLE_VALUE) CloseHandle(prompt->output);
#else
    if (prompt->fd >= 0) close(prompt->fd);
#endif
    mprotect_free(&prompt->input);
    mprotect_free(&prompt->line);
    free(prompt);

    if (err == UV_ENXIO) js_throw_error(env, "NO_TTY", "No terminal to prompt on");
    else secure_prompt_native__throw(env, err);

    return NULL;
  }

  err = mprotect_readwrite(&prompt->input);
  if (err < 0) err = secure_prompt_native__uv_error(err);

  if (err == 0) err = uv_tty_set_mode(&prompt->tty, UV_TTY_MODE_RAW);

  if (err == 0) err = uv_read_start((uv_stream_t *) &prompt->tty, secure_prompt_native__on_alloc, secure_prompt_native__on_read);

  if (err < 0) {
    uv_tty_set_mode(&prompt->tty, UV_TTY_MODE_NORMAL);
    mprotect_free(&prompt->input);
    uv_close((uv_handle_t *) &prompt->tty, secure_prompt_native__on_close);

    secure_prompt_native__throw(env, err);
    return NULL;
  }

  js_value_t *promise;
  err = js_create_promise(env, &prompt->deferred, &promise);
  assert(err == 0);

  return promise;
}

static js_value_t *
secure_prompt_native_take(js_env_t *env, js_callback_info_t *info) {
  int err;

  size_t argc = 2;
  js_value_t *argv[2];

  err = js_get_callback_info(env, info, &argc, argv, NULL, NULL);
  assert(err == 0);

  assert(argc >= 1);

  secure_prompt_native_result_t *result;
  err = js_get_value_external(env, argv[0], (void **) &result);
  assert(err == 0);

  if (argc == 2 && result->line.state != MPROTECT_FREED) {
    uint8_t *data;
    size_t len;
    err = js_get_typedarray_info(env, argv[1], NULL, (void **) &data, &len, NULL, NULL);
    assert(err == 0);

    assert(len == result->len);

    err = mprotect_readonly(&result->line);

    if (err < 0) {
      mprotect_free(&result->line);
      secure_prompt_native__throw(env, secure_prompt_native__uv_error(err));
      return NULL;
    }

    memcpy(data, result->line.data, result->len);
  }

  // Zeroes the internal copy
  mprotect_free(&result->line);

  return NULL;
}

static js_value_t *
secure_prompt_native_exports(js_env_t *env, js_value_t *exports) {
  int err;

#define V(name, fn) \
  { \
    js_value_t *val; \
    err = js_create_function(env, name, -1, fn, NULL, &val); \
    assert(err == 0); \
    err = js_set_named_property(env, exports, name, val); \
    assert(err == 0); \
  }

  V("prompt", secure_prompt_native_prompt)
  V("take", secure_prompt_native_take)
#undef V

  return exports;
}

BARE_MODULE(secure_prompt_native, secure_prompt_native_exports)
