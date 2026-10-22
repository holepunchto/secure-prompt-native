// Exits with a prompt still active, which must restore the terminal and not
// block the runtime draining the loop
const prompt = require('../..')

prompt({ message: 'secret: ' }).catch(() => {})

setTimeout(() => {
  if (typeof Bare !== 'undefined') Bare.exit(0)
  else process.exit(0)
}, 200)
