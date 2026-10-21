const { alloc } = require('mprotect-native')
const binding = require('./binding')

let active = false

module.exports = async function prompt(opts = {}) {
  const { message = '', maxLength = 4096 } = opts

  if (typeof message !== 'string') throw new TypeError('message must be a string')

  if (!Number.isSafeInteger(maxLength) || maxLength < 1) {
    throw new TypeError('maxLength must be a positive integer')
  }

  // There is only one terminal to read from
  if (active) {
    const err = new Error('A prompt is already active')
    err.code = 'ACTIVE'
    throw err
  }

  active = true

  let result, len

  try {
    ;[result, len] = await binding.prompt(message, maxLength)
  } finally {
    active = false
  }

  let memory

  try {
    memory = alloc(len)
  } catch (err) {
    binding.take(result)
    throw err
  }

  memory.write((view) => binding.take(result, view))

  return memory
}
