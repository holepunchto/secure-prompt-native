const { runtime } = require('which-runtime')

// Not require.resolve(), which Bare links ahead of time as a JavaScript module
const driver = __dirname + '/pty.py'
const fixture = require.resolve('../fixtures/prompt.js')

const platform = typeof Bare !== 'undefined' ? Bare.platform : process.platform

exports.supported = platform === 'linux' || platform === 'darwin'

// Type each string in `keys` into its own prompt on a pseudo-terminal
exports.type = function type(keys, mode = 'tty') {
  const args = [driver, execPath(), fixture, mode, JSON.stringify(keys)]

  const { stdout } = spawnSync('python3', args)

  return JSON.parse(stdout.toString())
}

function execPath() {
  return runtime === 'bare' ? require('bare-os').execPath() : process.execPath
}

function spawnSync(file, args) {
  if (runtime === 'bare') return require('bare-subprocess').spawnSync(file, args)
  return require('child_process').spawnSync(file, args)
}
