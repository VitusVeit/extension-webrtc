# Defold webrtc extension


## About

A quick and easy library that adds support for webrtc to Defold. It makes use of [paullouisageneau's libdatachannel library](https://github.com/paullouisageneau/libdatachannel).
Currently **it only targets the Web platform** but a port for desktop and mobile platforms is in development.

## Setup

To install the library just add it to the dependencies list on your project.
Currently there aren't any stable releases, the library is still under a heavy phase of development.

## Usage

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