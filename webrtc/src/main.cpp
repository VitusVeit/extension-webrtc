// Extension lib defines
#define LIB_NAME "WebRTC"
#define MODULE_NAME "webrtc"

// Defold SDK
#ifndef DLIB_LOG_DOMAIN
#define DLIB_LOG_DOMAIN LIB_NAME
#endif
#include <dmsdk/sdk.h>

#include "main.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>


#if !defined(DM_PLATFORM_WINDOWS)
static std::vector<rtc::IceServer> BuildIceServers()
{
    std::vector<rtc::IceServer> servers;
    servers.reserve(ice_server_entries.size());

    for (const IceServerEntry& entry : ice_server_entries)
    {
        if (entry.hasCredentials)
        {
            servers.emplace_back(entry.hostname, entry.port, entry.username, entry.password);
        }
        else
        {
            servers.emplace_back(entry.hostname, entry.port);
        }
    }

    return servers;
}


static rtc::Configuration BuildConfiguration()
{
    rtc::Configuration config;
    config.iceServers = BuildIceServers();
    return config;
}
#endif

#if defined(DM_PLATFORM_WINDOWS)
static inline void* ToUserPtr(int value)
{
    return reinterpret_cast<void*>(static_cast<intptr_t>(value));
}

static inline int FromUserPtr(void* ptr)
{
    return static_cast<int>(reinterpret_cast<intptr_t>(ptr));
}

static std::string GetDataChannelLabel(int dc)
{
    char label[512] = {0};
    if (rtcGetDataChannelLabel(dc, label, (int)sizeof(label)) >= 0)
    {
        return std::string(label);
    }

    return std::string();
}

static std::string FindLabelByDc(int peer_id, int dc)
{
    auto peer_it = data_channel_map.find(peer_id);
    if (peer_it == data_channel_map.end())
        return std::string();

    for (const auto& kv : peer_it->second)
    {
        if (kv.second == dc)
            return kv.first;
    }

    return std::string();
}

static void RTC_API OnPcStateChange(int pc, rtcState state, void* ptr)
{
    (void)pc;
    (void)ptr;
    dmLogDebug("State: %d", (int)state);
}

static void RTC_API OnPcGatheringStateChange(int pc, rtcGatheringState state, void* ptr)
{
    (void)pc;
    (void)ptr;
    dmLogDebug("Gathering State: %d", (int)state);
}

static void RTC_API OnPcLocalDescription(int pc, const char* sdp, const char* type, void* ptr)
{
    (void)pc;
    const int id = FromUserPtr(ptr);
    const char* safe_sdp = sdp ? sdp : "";
    const char* safe_type = type ? type : "";

    std::string message = std::to_string(id) + "@EOS@" + safe_type + "@EOS@" + safe_sdp;
    HandleCallback(EVENT_SIGNALING_DATA, 0, "", message);
}

static void RTC_API OnPcLocalCandidate(int pc, const char* cand, const char* mid, void* ptr)
{
    (void)pc;
    const int id = FromUserPtr(ptr);
    const char* safe_cand = cand ? cand : "";
    const char* safe_mid = mid ? mid : "";

    std::string message =
        std::to_string(id) + "@EOS@" +
        "candidate@EOS@" +
        safe_cand + "@EOS@" +
        safe_mid;

    HandleCallback(EVENT_SIGNALING_DATA, 0, "", message);
}

static void RTC_API OnDcOpen(int dc, void* ptr)
{
    const int peer_id = FromUserPtr(ptr);
    std::string label = FindLabelByDc(peer_id, dc);
    HandleCallback(EVENT_CHANNEL_OPENED, peer_id, label);
}

static void RTC_API OnDcClosed(int dc, void* ptr)
{
    const int peer_id = FromUserPtr(ptr);
    std::string label = FindLabelByDc(peer_id, dc);
    HandleCallback(EVENT_CHANNEL_CLOSED, peer_id, label);
}

static void RTC_API OnDcMessage(int dc, const char* message, int size, void* ptr)
{
    const int peer_id = FromUserPtr(ptr);
    std::string label = FindLabelByDc(peer_id, dc);

    if (!message || size <= 0)
    {
        HandleCallback(EVENT_MESSAGE, peer_id, label, "");
        return;
    }

    HandleCallback(EVENT_MESSAGE, peer_id, label, std::string(message, (size_t)size));
}

static void RegisterDataChannelCallbacks(int peer_id, int dc)
{
    rtcSetUserPointer(dc, ToUserPtr(peer_id));
    rtcSetOpenCallback(dc, OnDcOpen);
    rtcSetClosedCallback(dc, OnDcClosed);
    rtcSetMessageCallback(dc, OnDcMessage);
}

static void RTC_API OnPcDataChannel(int pc, int dc, void* ptr)
{
    (void)pc;
    const int peer_id = FromUserPtr(ptr);
    std::string label = GetDataChannelLabel(dc);

    if (label.empty())
    {
        label = std::string("dc_") + std::to_string(dc);
    }

    data_channel_map[peer_id][label] = dc;
    RegisterDataChannelCallbacks(peer_id, dc);

    dmLogDebug("DataChannel from %d received with label \"%s\", is dt open? %d", peer_id, label.c_str(), rtcIsOpen(dc));
}
#endif


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

// we can't use std::atoi since that throws an exception, and exceptions are disabled
static bool parse_int(const std::string& value, int* out)
{
    if (!out || value.empty())
        return false;

    char* end = nullptr;
    errno = 0;
    long parsed = std::strtol(value.c_str(), &end, 10);
    if (errno != 0 || end == value.c_str() || *end != '\0')
        return false;

    *out = (int)parsed;
    return true;
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


#if defined(DM_PLATFORM_WINDOWS)
static int create_peer(int id)
{
    std::vector<std::string> urls;
    urls.reserve(ice_server_entries.size());

    for (const IceServerEntry& entry : ice_server_entries)
    {
        if (entry.hasCredentials)
        {
            urls.push_back(
                std::string("turn:") + entry.username + ":" + entry.password + "@" +
                entry.hostname + ":" + std::to_string(entry.port)
            );
        }
        else
        {
            urls.push_back(std::string("stun:") + entry.hostname + ":" + std::to_string(entry.port));
        }
    }

    std::vector<const char*> ice_server_ptrs;
    ice_server_ptrs.reserve(urls.size());
    for (const std::string& url : urls)
    {
        ice_server_ptrs.push_back(url.c_str());
    }

    rtcConfiguration config = {};
    config.iceServers = ice_server_ptrs.empty() ? nullptr : ice_server_ptrs.data();
    config.iceServersCount = (int)ice_server_ptrs.size();

    int pc = rtcCreatePeerConnection(&config);
    if (pc < 0)
    {
        dmLogError("Failed to create PeerConnection for id %d (error %d)", id, pc);
        return -1;
    }

    rtcSetUserPointer(pc, ToUserPtr(id));
    rtcSetStateChangeCallback(pc, OnPcStateChange);
    rtcSetGatheringStateChangeCallback(pc, OnPcGatheringStateChange);
    rtcSetLocalDescriptionCallback(pc, OnPcLocalDescription);
    rtcSetLocalCandidateCallback(pc, OnPcLocalCandidate);
    rtcSetDataChannelCallback(pc, OnPcDataChannel);

    peer_connection_map[id] = pc;
    return pc;
}
#else
static std::shared_ptr<rtc::PeerConnection> create_peer(int id) 
{
    std::shared_ptr<rtc::PeerConnection> pc;

    pc = std::make_shared<rtc::PeerConnection>(BuildConfiguration());
    
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
                dmLogWarning("Binary message from %d received, size = %zu", id, std::get<rtc::binary>(data).size());
        });

        data_channel_map[id].emplace(dc->label(), dc);
    });

    peer_connection_map.emplace(id, pc);
    return pc;
};
#endif


static void create_channel(int id, std::string label, int type)
{
#if defined(DM_PLATFORM_WINDOWS)
    if (auto jt = data_channel_map[id].find(label); jt != data_channel_map[id].end())
        return;

    int pc = -1;
    if (auto jt = peer_connection_map.find(id); jt != peer_connection_map.end())
        pc = jt->second;
    else
        pc = create_peer(id);

    if (pc < 0)
        return;

    dmLogDebug("Creating DataChannel with label \"%s\"", label.c_str());

    if (type < TYPE_UNRELIABLE or type > TYPE_RELIABLE)
    {
        dmLogError("Expected a TYPE_* enum on 'create_channel'  (got %d)", type);
        return;
    }

    rtcDataChannelInit in = {};
    in.protocol = "";
    in.negotiated = false;
    in.manualStream = false;

    switch (type)
    {
    case TYPE_UNRELIABLE:
        in.reliability.unordered = true;
        in.reliability.unreliable = true;
        in.reliability.maxRetransmits = 0;
        break;
    case TYPE_UNRELIABLE_ORDERED:
        in.reliability.unordered = false;
        in.reliability.unreliable = true;
        in.reliability.maxRetransmits = 0;
        break;
    case TYPE_RELIABLE:
        in.reliability.unordered = false;
        in.reliability.unreliable = false;
        break;
    }

    int dc = rtcCreateDataChannelEx(pc, label.c_str(), &in);
    if (dc < 0)
    {
        dmLogError("Failed to create DataChannel with label '%s' (error %d)", label.c_str(), dc);
        return;
    }

    data_channel_map[id][label] = dc;
    RegisterDataChannelCallbacks(id, dc);

    if (rtcIsOpen(dc))
    {
        HandleCallback(EVENT_CHANNEL_OPENED, id, label);
    }
#else
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
        rel.unordered = true;
        rel.maxRetransmits = 0;
        break;
    case TYPE_UNRELIABLE_ORDERED:
        rel.unordered = false;
        rel.maxRetransmits = 0;
        break;
    case TYPE_RELIABLE:
        rel.unordered = false;
        break;
    }

    rtc::DataChannelInit in;
    in.reliability = rel;
    
    auto dc = pc->createDataChannel(label, in);
    if (!dc)
    {
        dmLogError("Failed to create DataChannel with label '%s'", label.c_str());
        return;
    }

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
            dmLogWarning("Binary message from %d received, size = %zu", id, std::get<rtc::binary>(data).size());
    });

    data_channel_map[id].emplace(dc->label(), dc);
#endif
}


static void process_data(std::string message)
{
#if defined(DM_PLATFORM_WINDOWS)
    message.erase(remove(message.begin(), message.end(), '"'),message.end());

    std::vector<std::string> data = split(message, "@EOS@");
    if (data.size() < 2)
    {
        dmLogError("Invalid signaling payload '%s'", message.c_str());
        return;
    }

    int id = 0;
    if (!parse_int(data[0], &id))
    {
        dmLogError("Invalid peer id '%s' in signaling payload", data[0].c_str());
        return;
    }

    const std::string type = data[1];

    int pc = -1;
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
    {
        dmLogError("Received data of type '%s' from peer %d, but there isn't any peer connection in place (didn't receive an offer first)", type.c_str(), id);
        return;
    }

    if (pc < 0)
        return;

    if (type == "offer" || type == "answer")
    {
        if (data.size() < 3)
        {
            dmLogError("Invalid %s payload from peer %d", type.c_str(), id);
            return;
        }

        int rc = rtcSetRemoteDescription(pc, data[2].c_str(), type.c_str());
        if (rc < 0)
        {
            dmLogError("Failed to set remote description from peer %d (error %d)", id, rc);
        }
    }
    else if (type == "candidate")
    {
        if (data.size() < 4)
        {
            dmLogError("Invalid candidate payload from peer %d", id);
            return;
        }

        int rc = rtcAddRemoteCandidate(pc, data[2].c_str(), data[3].c_str());
        if (rc < 0)
        {
            dmLogError("Failed to add remote candidate from peer %d (error %d)", id, rc);
        }
    }
#else
    message.erase(remove(message.begin(), message.end(), '\"'),message.end());

    std::vector<std::string> data = split(message, "@EOS@");
    if (data.size() < 2)
    {
        dmLogError("Invalid signaling payload '%s'", message.c_str());
        return;
    }
    
    int id = 0;
    if (!parse_int(data[0], &id))
    {
        dmLogError("Invalid peer id '%s' in signaling payload", data[0].c_str());
        return;
    }

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
    {
        dmLogError("Received data of type '%s' from peer %d, but there isn't any peer connection in place (didn't receive an offer first)", type.c_str(), id);
        return;
    }

    if (type == "offer" || type == "answer") 
    {
        if (data.size() < 3)
        {
            dmLogError("Invalid %s payload from peer %d", type.c_str(), id);
            return;
        }

        auto sdp = data[2];
        pc->setRemoteDescription(rtc::Description(sdp, type));
    } 
    else if (type == "candidate") 
    {
        if (data.size() < 4)
        {
            dmLogError("Invalid candidate payload from peer %d", id);
            return;
        }

        auto sdp = data[2];
        auto mid = data[3];
        pc->addRemoteCandidate(rtc::Candidate(sdp, mid));
    }
#endif
}


static void send_message(int id, std::string label, std::string message)
{
#if defined(DM_PLATFORM_WINDOWS)
    if (not data_channel_map.count(id)) {
        dmLogError("No peer found with id %d on \'send_message\'", id);
        return;
    }

    if (not data_channel_map[id].count(label)) {
        dmLogError("No channel \"%s\" found for id %d on \'send_message\'", label.c_str(), id);
        return;
    }

    int dc = data_channel_map[id][label];
    int rc = rtcSendMessage(dc, message.c_str(), (int)message.size());
    if (rc < 0)
    {
        dmLogError("Failed to send message to peer %d on channel '%s' (error %d)", id, label.c_str(), rc);
    }
#else
    if (not data_channel_map.count(id)) {
        dmLogError("No peer found with id %d on \'send_message\'", id);
        return;
    }

    if (not data_channel_map[id].count(label)) {
        dmLogError("No channel \"%s\" found for id %d on \'send_message\'", label.c_str(), id);
        return;
    }
       
    data_channel_map[id][label]->send(message);
#endif
}


static int LuaSetConfiguration(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 0);

    ice_server_entries.clear();
    
    // Handle stuns and turns lists.
    std::vector<std::string> result = split(luaL_checkstring(L, 1), ";");
    
    for (std::string& section : result)
    {
        std::vector<std::string> zone = split(section, ":");
        int port = 0;

        if (zone.size() == 2)
        {
            if (!parse_int(zone[1], &port))
            {
                dmLogError("Invalid port '%s' in ice server entry '%s'", zone[1].c_str(), section.c_str());
                continue;
            }

            IceServerEntry entry;
            entry.hostname = zone[0];
            entry.port = (uint16_t)port;
            entry.hasCredentials = false;
            ice_server_entries.push_back(std::move(entry));
        }
        else if (zone.size() == 4)
        {
            if (!parse_int(zone[1], &port))
            {
                dmLogError("Invalid port '%s' in ice server entry '%s'", zone[1].c_str(), section.c_str());
                continue;
            }

            IceServerEntry entry;
            entry.hostname = zone[0];
            entry.port = (uint16_t)port;
            entry.username = zone[2];
            entry.password = zone[3];
            entry.hasCredentials = true;
            ice_server_entries.push_back(std::move(entry));
        }
        else
            dmLogError("Expected 2 or 4 arguments for an ice server entry, got %zu, on \'set_configuration\'", zone.size());
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