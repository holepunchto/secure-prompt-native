const test = require('brittle')
const prompt = require('..')

test('validates options', async (t) => {
  await t.exception.all(prompt({ message: 1 }), /message must be a string/)
  await t.exception.all(prompt({ maxLength: 0 }), /maxLength must be a positive integer/)
  await t.exception.all(prompt({ maxLength: 1.5 }), /maxLength must be a positive integer/)
})
