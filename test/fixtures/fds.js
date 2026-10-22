// Reports the number of open descriptors after each prompt
const prompt = require('../..')
const fs = typeof Bare !== 'undefined' ? require('bare-fs') : require('fs')

const argv = typeof Bare !== 'undefined' ? Bare.argv : process.argv
const count = Number(argv[argv.length - 1])

main()

async function main() {
  for (let i = 0; i < count; i++) {
    const memory = await prompt({ message: 'secret: ' })
    memory.free()

    console.log('RESULT ' + JSON.stringify({ fds: fs.readdirSync('/proc/self/fd').length }))
  }
}
