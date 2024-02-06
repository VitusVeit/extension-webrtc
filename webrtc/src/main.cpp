// Extension lib defines
#define LIB_NAME "WebRTC"
#define MODULE_NAME "webrtc"

// Defold SDK
#define DLIB_LOG_DOMAIN LIB_NAME
#include <dmsdk/sdk.h>

#include "main.hpp"


std::vector<std::string> split(std::string string, const std::string& delimiter)
{
    std::vector<std::string> result{};

    std::size_t pos = 0;
    std::string token;
    while ((pos = string.find(delimiter)) != std::string::npos) 
    {
        token = string.substr(0, pos);
        if (not token.empty())
        result.push_back(token);
        string.erase(0, pos + delimiter.length());
    }

    if (not string.empty())
        result.push_back(string);

    return result;
}


void HandleCallback(int event, int id, std::string label, std::string data)
{
    if (!dmScript::IsCallbackValid(webrtc_callback))
    {
        dmLogError("WebRTC callback is invalid");
        return;
    }

    // Call Callback to get message.
    lua_State* L = dmScript::GetCallbackLuaContext(webrtc_callback);
    DM_LUA_STACK_CHECK(L, 0);

    if (!dmScript::SetupCallback(webrtc_callback))
    {
        dmLogError("Failed to setup WebRTC callback");
        return;
    }

    lua_pushnumber(L, lua_Number(event));

    lua_newtable(L);
    
    if (event < EVENT_CHANNEL_OPENED or event > EVENT_MESSAGE)
    {
        dmLogError("Expected an EVENT_* enum for WebRTC callback (got %d)", event);
    }
    
    if (event != EVENT_SIGNALING_DATA)
    {    
        lua_pushnumber(L, id);
        lua_setfield(L, -2, "id");

        lua_pushstring(L, label.c_str());
        lua_setfield(L, -2, "label");
    }

    if (event == EVENT_SIGNALING_DATA)
    {
        lua_pushstring(L, data.c_str());
        lua_setfield(L, -2, "signaling");
    }
    else if (event == EVENT_MESSAGE)
    {
        lua_pushstring(L, data.c_str());
        lua_setfield(L, -2, "msg");
    }

    dmScript::PCall(L, 3, 0);

    dmScript::TeardownCallback(webrtc_callback);
}


static std::shared_ptr<rtc::PeerConnection> create_peer(int id) 
{   
    std::shared_ptr<rtc::PeerConnection> pc = std::make_shared<rtc::PeerConnection>(configuration);
    
    pc->onStateChange([](rtc::PeerConnection::State state) { dmLogDebug("State: %d", int(state)); });

    pc->onGatheringStateChange([](rtc::PeerConnection::GatheringState state) 
    {
        dmLogDebug("Gathering State: %d", int(state));
    });

    pc->onLocalDescription([id](rtc::Description description)
    {
        // ID				id
        // TYPE				description.typeString()
        // DESCRIPTION		std::string(description)

        std::string message = 
        std::to_string(id) + "@EOS@" +
        description.typeString() + "@EOS@" +
        std::string(description);
        
        HandleCallback(EVENT_SIGNALING_DATA, 0, "", message);
    });

    pc->onLocalCandidate([id](rtc::Candidate candidate)
    {
        // ID				id
        // TYPE				"candidate"
        // CANDIDATE		std::string(candidate)
        // MID				candidate.mid()

        std::string message =
        std::to_string(id) + "@EOS@" +
        "candidate" + "@EOS@" +
        std::string(candidate) + "@EOS@" +
        candidate.mid();

        HandleCallback(EVENT_SIGNALING_DATA, 0, "", message);
    });

    pc->onDataChannel([id](std::shared_ptr<rtc::DataChannel> dc)
    {
        dmLogDebug("DataChannel from %d received with label \"%s\", is dt open? %d", id, dc->label().c_str(), dc->isOpen());

        dc->onOpen([id, dc]()
        {
            HandleCallback(EVENT_CHANNEL_OPENED, id, dc->label());
        });

        dc->onClosed([id, dc]()
        { 
            HandleCallback(EVENT_CHANNEL_CLOSED, id, dc->label());
        });

        dc->onMessage([id, dc](auto data)
        {
            // Data holds either std::string or rtc::binary.
            if (std::holds_alternative<std::string>(data))
            {
                HandleCallback(EVENT_MESSAGE, id, dc->label(), std::get<std::string>(data));
            }
            else
                dmLogWarning("Binary message from %d received, size = %lu", id, std::get<rtc::binary>(data).size());
        });

        data_channel_map[id].emplace(dc->label(), dc);
    });

    peer_connection_map.emplace(id, pc);
    return pc;
};


static void create_channel(int id, std::string label, int type)
{
    // Check if there's already a channel with the same label open.
    if (auto jt = data_channel_map[id].find(label); jt != data_channel_map[id].end())
        return;
    
    std::shared_ptr<rtc::PeerConnection> pc;
    
    if (auto jt = peer_connection_map.find(id); jt != peer_connection_map.end()) 
        pc = jt->second;
    else
        pc = create_peer(id);
    
    // We are the offerer, so create a data channel to initiate the process.
    dmLogDebug("Creating DataChannel with label \"%s\"", label.c_str());
    
    if (type < TYPE_UNRELIABLE or type > TYPE_RELIABLE)
    {
        dmLogError("Expected a TYPE_* enum on 'create_channel'  (got %d)", type);
    }
    
    rtc::Reliability rel;
    
    // NOTE: 
    // This way of setting the reliability works only for the libdatachannel-wasm library, 
    // in the base one it's deprecated and another method should be used.
    switch (type)
    {
    case TYPE_UNRELIABLE:
        rel.type = rtc::Reliability::Type::Rexmit;
        rel.unordered = true;
        break;
    case TYPE_UNRELIABLE_ORDERED:
        rel.type = rtc::Reliability::Type::Rexmit;
        rel.unordered = false;
        break;
    case TYPE_RELIABLE:
        rel.type = rtc::Reliability::Type::Reliable;
        rel.unordered = false;
        break;
    }

    rtc::DataChannelInit in;
    in.reliability = rel;
    
    auto dc = pc->createDataChannel(label, in);

    dc->onOpen([id, dc]() 
    {
        HandleCallback(EVENT_CHANNEL_OPENED, id, dc->label());
    });

    dc->onClosed([id, dc]()
    {
        HandleCallback(EVENT_CHANNEL_CLOSED, id, dc->label());
    });

    dc->onMessage([id, dc](auto data)
    {
        // Data holds either std::string or rtc::binary.
        if (std::holds_alternative<std::string>(data))
        {
            HandleCallback(EVENT_MESSAGE, id, dc->label(), std::get<std::string>(data));
        }
        else
            dmLogWarning("Binary message from %d received, size = %ld", id, std::get<rtc::binary>(data).size());
    });

    data_channel_map[id].emplace(dc->label(), dc);
}


static void process_data(std::string message)
{
    message.erase(remove(message.begin(), message.end(), '\"'),message.end());

    std::vector<std::string> data = split(message, "@EOS@");
    
    int id = std::stoi(data[0]);
    std::string type = data[1];

    std::shared_ptr<rtc::PeerConnection> pc;
    
    if (auto jt = peer_connection_map.find(id); jt != peer_connection_map.end())
    {
        pc = jt->second;
    }
    else if (type == "offer") 
    {
        dmLogDebug("Generating an answer to %d", id);
        pc = create_peer(id);
    } 
    else
        dmLogError("Received data of type '%s' from peer %d, but there isn't any peer connection in place (didn't receive an offer first)", type.c_str(), id);

    if (type == "offer" || type == "answer") 
    {
        auto sdp = data[2];
        pc->setRemoteDescription(rtc::Description(sdp, type));
    } 
    else if (type == "candidate") 
    {
        auto sdp = data[2];
        auto mid = data[3];
        pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
    }
}


static void send_message(int id, std::string label, std::string message)
{
    if (not data_channel_map.count(id)) {
        dmLogError("No peer found with id %d on \'send_message\'", id);
        return;
    }

    if (not data_channel_map[id].count(label)) {
        dmLogError("No channel \"%s\" found for id %d on \'send_message\'", label.c_str(), id);
        return;
    }
       
    data_channel_map[id][label]->send(message);
}


static int LuaSetConfiguration(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);
    
    // Handle stuns and turns lists.
    std::vector<std::string> result = split(luaL_checkstring(L, 1), ";");
    
    for (std::string& section : result)
    {
        std::vector<std::string> zone = split(section, ":");

        if (zone.size() == 2)
            configuration.iceServers.push_back(rtc::IceServer(zone[0].c_str(), std::stoi(zone[1])));
        else if (zone.size() == 4)
            configuration.iceServers.push_back(rtc::IceServer(zone[0].c_str(), std::stoi(zone[1]), zone[2].c_str(), zone[3].c_str()));
        else
            dmLogError("Expected 2 or 4 arguments for an ice server entry, got %lu, on \'set_configuration\'", zone.size());
    }

    webrtc_callback = dmScript::CreateCallback(L, 2);  
    
    return 0;
}


static int LuaProcessData(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    const char* data = luaL_checkstring(L, 1);

    process_data(data);
    
    return 0;
}

static int LuaCreateChannel(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    int id = luaL_checknumber(L, 1);
    const char* label = luaL_checkstring(L, 2);
    int type = luaL_checknumber(L, 3);

    create_channel(id, label, type);

    return 0;
}

static int LuaSendMessage(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    int id = luaL_checknumber(L, 1);
    const char* label = luaL_checkstring(L, 2);
    const char* message = luaL_checkstring(L, 3);

    send_message(id, label, message);

    return 0;
}

// Functions exposed to Lua
static const luaL_reg Module_methods[] =
{
    {"set_configuration", LuaSetConfiguration},
    {"process_data", LuaProcessData},
    {"create_channel", LuaCreateChannel},
    {"send_message", LuaSendMessage},
    {0, 0}
};

static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);

    // Register lua names.
    luaL_register(L, MODULE_NAME, Module_methods);

    // Set constants.
    
#define SET_CONSTANT(_X) \
    lua_pushnumber(L, (lua_Number) _X); \
    lua_setfield(L, -2, #_X);

    SET_CONSTANT(TYPE_UNRELIABLE);
    SET_CONSTANT(TYPE_UNRELIABLE_ORDERED);
    SET_CONSTANT(TYPE_RELIABLE);

    SET_CONSTANT(EVENT_CHANNEL_OPENED);
    SET_CONSTANT(EVENT_CHANNEL_CLOSED);
    SET_CONSTANT(EVENT_SIGNALING_DATA);
    SET_CONSTANT(EVENT_MESSAGE);
    
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

static dmExtension::Result AppInitializeExtension(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeExtension(dmExtension::Params* params)
{
    // Init Lua
    LuaInit(params->m_L);
    dmLogInfo("Registered %s extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

/*#else

static dmExtension::Result AppInitializeExtension(dmExtension::AppParams* params)
{
    dmLogWarning("Registered %s (null) extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeExtension(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

#endif*/

static dmExtension::Result AppFinalizeExtension(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeExtension(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(WebRTC, LIB_NAME, AppInitializeExtension, AppFinalizeExtension, InitializeExtension, 0, 0, FinalizeExtension)