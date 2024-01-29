# Defold webrtc extension


## About

A quick and easy library that adds support for webrtc to Defold. It makes use of paullouisageneau's webrtc c++ library, libdatachannel.
Currently it only targets the Web platform but porting it to desktop and mobile platforms won't be a problem.

## Setup

To install the library just add it to the dependencies list on your project.

## Usage

The library already features a lua documentation also in the examples folder there is a small test that uses the websocket library.

Here's a small overview of the library:

```lua
-- Called as soon as a signaling data it's created. (eg. offer, answer, candidates)
function on_signaling_data(self, data)
    server.send(data)
end

function on_channel_info(self, event. id, label)
    if event == "opened" then
        print("Data channel \'" .. label .. "\' with " .. id .. " created!")
        webrtc.send_message(id, "Hello!")
    elseif event == "closed" then
        print("Data channel \'" .. label .. "\' with " .. id .. " closed!")
    end
end

function on_channel_message(self, id, label, msg)
    print("[" .. id .. "] Got message from \'" .. label .. "\':" .. msg)
end

-- We're assuming the server only sends signaling data.
function on_server_message(self, message)
    webrtc.process_data(message)
end

function init()
    webrtc.set_configuration("stun_url:PORT;turn_url:PORT:USER:PASSWORD", on_signaling_data, on_channel_info, on_channel_message)

    -- If we're the offerer, create a channel with another peer.
    webrtc.create_channel(peer_id, "my_channel_name", webrtc.TYPE_RELIABLE)
end
```