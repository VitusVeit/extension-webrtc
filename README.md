# Defold webrtc extension


## About

A quick and easy library that adds support for webrtc to Defold. It makes use of [paullouisageneau's libdatachannel library](https://github.com/paullouisageneau/libdatachannel).

The wasm version is based off of [datachannel-wasm](https://github.com/paullouisageneau/datachannel-wasm).

Right now, it only works on WASM and Windows, but theoretically it should work on more platforms if the right binaries are compiled and linked.

## Setup

To install the library, simply add it to your project's dependencies list.
Currently there aren't any stable releases, the library is still under a heavy phase of development.

## Usage

When you build the web version, it will be served at http://localhost:43763/html5/index.html as long as defold is open.

The library already features a lua documentation, in the examples folder there is also a small test that uses the defold websocket extension.

Here's a small overview of the extension:

```lua
function webrtc_callback(self, event, data)
    if event == webrtc.EVENT_CHANNEL_OPENED then
        print("Data channel \'" .. data.label .. "\' with " .. data.id .. " created!")
        webrtc.send_message(id, label, "Hello!")

    elseif event == webrtc.EVENT_CHANNEL_CLOSED then
        print("Data channel \'" .. data.label .. "\' with " .. data.id .. " closed!")

    elseif event == webrtc.EVENT_SIGNALING_DATA then
        server.send(data.signaling)

    elseif event == webrtc.EVENT_MESSAGE then
        print("[" .. data.id .. "] Got message from \'" .. data.label .. "\':" .. data.msg)
    end
end

function on_server_message(self, message)
    webrtc.process_data(message)
end

function init()
    webrtc.set_configuration("stun_url:PORT;turn_url:PORT:USER:PASSWORD", webrtc_callback)
    -- If we're the offerer, create a channel with another peer.
    webrtc.create_channel(peer_id, "my_channel_name", webrtc.TYPE_RELIABLE)
end
```
