const test = require('brittle')
const { type, supported } = require('./helpers')

const skip = !supported

const bytes = (s) => Array.from(s, (c) => c.charCodeAt(0))

test('returns the line as secure memory', { skip }, (t) => {
  const { results, restored } = type(['hunter2\r'])

  t.alike(results, [{ value: bytes('hunter2'), state: 'noaccess' }])
  t.ok(restored, 'terminal settings restored')
})

test('newline also submits', { skip }, (t) => {
  t.alike(type(['pw\n']).results, [{ value: bytes('pw'), state: 'noaccess' }])
})

test('empty line rejects', { skip }, (t) => {
  t.alike(type(['\r']).results, [{ error: 'NO_INPUT' }])
})

test('ctrl-c and ctrl-d cancel', { skip }, (t) => {
  const { results, restored } = type(['ab\x03', '\x04'])

  t.alike(results, [{ error: 'CANCELLED' }, { error: 'CANCELLED' }])
  t.ok(restored)
})

test('input longer than maxLength rejects', { skip }, (t) => {
  const { results } = type(['a'.repeat(4097), 'a'.repeat(4096) + '\r'])

  t.alike(results[0], { error: 'TOO_LONG' })
  t.is(results[1].value.length, 4096)
})

test('escape sequences and other control keys are ignored', { skip }, (t) => {
  const keys = 'ab\x1b[D\x1b[A\x1b[1;5C\x1bOH\x1bOP\x1bx\t\x1b[200~c\x1b[201~\r'

  t.alike(type([keys]).results, [{ value: bytes('abc'), state: 'noaccess' }])
})

test('backspace removes whole utf-8 characters', { skip }, (t) => {
  const keys = 'h\xc3\xa9\xe2\x82\xac\x7f\x7f!\xf0\x9f\x98\x80\x08\r'

  t.alike(type([keys]).results, [{ value: bytes('h!'), state: 'noaccess' }])
})

test('backspace on an empty line is harmless', { skip }, (t) => {
  t.alike(type(['\x7f\x7fok\r']).results, [{ value: bytes('ok'), state: 'noaccess' }])
})

test('prompts work again after a failed one', { skip }, (t) => {
  const { results, restored } = type(['\r', '\x03', 'a'.repeat(4097), 'pw\r'])

  t.alike(results, [
    { error: 'NO_INPUT' },
    { error: 'CANCELLED' },
    { error: 'TOO_LONG' },
    { value: bytes('pw'), state: 'noaccess' }
  ])
  t.ok(restored)
})

test('no controlling terminal rejects', { skip }, (t) => {
  t.alike(type([], 'no-tty').results, [{ error: 'NO_TTY' }])
})

test('never echoes the secret, across consecutive prompts', { skip }, (t) => {
  const secrets = ['alpha', 'bravo', 'charlie', 'delta']
  const { results, output, restored } = type(secrets.map((s) => s + '\r'))

  t.alike(
    results,
    secrets.map((s) => ({ value: bytes(s), state: 'noaccess' }))
  )

  for (const s of secrets) t.absent(output.includes(s), s + ' not echoed')

  t.ok(restored)
})
