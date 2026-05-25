## About

This is a test that uses a simple websocket python server 'signaling-server.py' that acts as a signaling server and connects two peers
using an id that goes from 0 to 99.

## Usage

Start the signaling server, for example:

python signaling-server.py 127.0.0.1:8080

Then run the example (it uses ws://127.0.0.1:8080 by default in example.gui_script).

Just export the project to the Web platform and, when opened in a tab, type in the other user's id, you should see a "Data channel x with y opened!" message on the console upon successfully connecting.

If there's need to use a STUN or TURN server you can use [violet](https://github.com/paullouisageneau/violet), then just insert the credentials in the 'set_configuration' function.

By default the example uses an empty WebRTC configuration. If you set STUN/TURN entries, use real numeric ports (for example `stun.example.com:3478`) and avoid placeholders like `PORT`.