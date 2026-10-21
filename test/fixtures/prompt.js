const prompt = require('../..')

const argv = typeof Bare !== 'undefined' ? Bare.argv : process.argv
const count = Number(argv[argv.length - 1])

main()

async function main() {
  for (let i = 0; i < count; i++) {
    let result

    try {
      const memory = await prompt({ message: 'secret: ' })
      result = { value: memory.read((view) => Array.from(view)), state: memory.state }
      memory.free()
    } catch (err) {
      result = { error: err.code }
    }

    console.log('RESULT ' + JSON.stringify(result))
  }
}
