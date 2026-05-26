// The datachannel library.
#if defined(DM_PLATFORM_HTML5)
#include <emscripten.h>
#include "rtc-wasm/rtc.hpp"
#else
#include "rtc/rtc.hpp"
#endif


#include <string>
#include <vector>
#include <unordered_map>
#include <memory>


struct IceServerEntry {
	std::string hostname;
	uint16_t port;
	std::string username;
	std::string password;
	bool hasCredentials;
};

std::vector<IceServerEntry> ice_server_entries;


#if !defined(DM_PLATFORM_HTML5)
std::unordered_map<int, int> peer_connection_map;
std::unordered_map<int, std::unordered_map<std::string, int>> data_channel_map;
#else
std::unordered_map<int, std::shared_ptr<rtc::PeerConnection>> peer_connection_map;
std::unordered_map<int, std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>>> data_channel_map;
#endif


dmScript::LuaCallbackInfo* webrtc_callback = NULL;


enum Type
{
	TYPE_UNRELIABLE,
	TYPE_UNRELIABLE_ORDERED,
	TYPE_RELIABLE,
};


enum Event
{
	EVENT_CHANNEL_OPENED,
	EVENT_CHANNEL_CLOSED,
	EVENT_SIGNALING_DATA,
	EVENT_MESSAGE,
};


void HandleCallback(int event, int id, std::string label, std::string data = "");

#if !defined(DM_PLATFORM_HTML5)
static int create_peer(int id);
#else
static std::shared_ptr<rtc::PeerConnection> create_peer(int id);
#endif

static void create_channel(int id, std::string label, int type);

static void process_data(std::string message);

static void send_message(int id, std::string label, std::string message);

static int LuaSetConfiguration(lua_State* L);

static int LuaProcessData(lua_State* L);

static int LuaCreateChannel(lua_State* L);

static int LuaSendMessage(lua_State* L);