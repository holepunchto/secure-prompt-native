# secure-prompt-native

Prompt for secrets on the terminal straight into guarded memory, for Bare and Node.js.

```
npm i secure-prompt-native
```

Input is read from the controlling terminal (`/dev/tty`, or the console on Windows) rather than stdin, with echo off. Every byte lands in memory from [libmprotect](https://github.com/holepunchto/libmprotect) and the result is handed over as a [`mprotect-native`](https://github.com/holepunchto/mprotect-native) `SecureMemory`, so the secret never touches the JavaScript heap.

## Usage

```js
const prompt = require('secure-prompt-native')

const password = await prompt({ message: 'Password: ' })

await password.read((view) => sodium.crypto_pwhash_async(key, view, salt, ops, mem, alg))

password.free()
```

## API

#### `const memory = await prompt([options])`

Prompt for a line and resolve with a `SecureMemory` holding it, in the `noaccess` state.

Options include:

```js
options = {
  message: '', // Written to the terminal before reading
  maxLength: 4096 // Longest accepted line, in bytes
}
```

Enter submits the line. Backspace removes a whole UTF-8 character, and escape sequences such as arrow keys, as well as other control keys, are ignored.

Rejects with an error whose `code` is one of:

- `CANCELLED`: Ctrl-C or Ctrl-D was pressed
- `NO_INPUT`: the line was empty
- `TOO_LONG`: the line was longer than `maxLength`
- `EOF`: the terminal closed
- `NO_TTY`: there is no terminal to prompt on, such as when running detached or in CI
- `ACTIVE`: another prompt is already active

## Caveats

The terminal is left in raw mode if the process is killed by a signal while a prompt is active.

## License

Apache-2.0

