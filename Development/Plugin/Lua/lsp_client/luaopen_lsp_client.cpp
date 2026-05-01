#include <lua.hpp>
#include "lsp_client.h"
#include <string>
#include <cstring>

// 日志回调
static int logCallbackRef = LUA_NOREF;
static lua_State* logCallbackState = nullptr;

static void lspLogCallback(int level, const std::string& message) {
    if (logCallbackState && logCallbackRef != LUA_NOREF) {
        lua_rawgeti(logCallbackState, LUA_REGISTRYINDEX, logCallbackRef);
        lua_pushinteger(logCallbackState, level);
        lua_pushstring(logCallbackState, message.c_str());
        lua_pcall(logCallbackState, 2, 0, 0);
    }
}

// 消息回调
static int messageCallbackRef = LUA_NOREF;
static lua_State* messageCallbackState = nullptr;

static void lspMessageCallback(const std::string& jsonMessage) {
    if (messageCallbackState && messageCallbackRef != LUA_NOREF) {
        lua_rawgeti(messageCallbackState, LUA_REGISTRYINDEX, messageCallbackRef);
        lua_pushstring(messageCallbackState, jsonMessage.c_str());
        lua_pcall(messageCallbackState, 1, 0, 0);
    }
}

// lsp_client.config(config_table)
static int lsp_client_config(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    ydwe::lsp::client::ClientConfig config;
    
    if (lua_istable(L, 1)) {
        lua_getfield(L, 1, "serverPath");
        if (lua_isstring(L, -1)) {
            config.serverPath = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        
        lua_getfield(L, 1, "workspacePath");
        if (lua_isstring(L, -1)) {
            config.workspacePath = lua_tostring(L, -1);
        }
        lua_pop(L, 1);
        
        // logLevel field is optional, defaults to 1 (error level)
    }
    
    client->setConfig(config);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.setLogCallback(callback)
static int lsp_client_set_log_callback(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client || !lua_isfunction(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // 释放旧的回调
    if (logCallbackState && logCallbackRef != LUA_NOREF) {
        luaL_unref(logCallbackState, LUA_REGISTRYINDEX, logCallbackRef);
    }
    
    // 保存新的回调
    logCallbackState = L;
    lua_pushvalue(L, 1);
    logCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    
    client->setLogCallback(lspLogCallback);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.setMessageCallback(callback)
static int lsp_client_set_message_callback(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client || !lua_isfunction(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    // 释放旧的回调
    if (messageCallbackState && messageCallbackRef != LUA_NOREF) {
        luaL_unref(messageCallbackState, LUA_REGISTRYINDEX, messageCallbackRef);
    }
    
    // 保存新的回调
    messageCallbackState = L;
    lua_pushvalue(L, 1);
    messageCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    
    client->setMessageCallback(lspMessageCallback);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.start()
static int lsp_client_start(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    bool success = client->startServer();
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

// lsp_client.stop()
static int lsp_client_stop(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (client) {
        client->stopServer();
    }
    
    // 清理回调
    if (logCallbackState && logCallbackRef != LUA_NOREF) {
        luaL_unref(logCallbackState, LUA_REGISTRYINDEX, logCallbackRef);
        logCallbackRef = LUA_NOREF;
    }
    if (messageCallbackState && messageCallbackRef != LUA_NOREF) {
        luaL_unref(messageCallbackState, LUA_REGISTRYINDEX, messageCallbackRef);
        messageCallbackRef = LUA_NOREF;
    }
    
    ydwe::lsp::client::shutdownGlobalClient();
    return 0;
}

// lsp_client.isRunning()
static int lsp_client_is_running(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    lua_pushboolean(L, client->isRunning() ? 1 : 0);
    return 1;
}

// lsp_client.poll()
static int lsp_client_poll(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (client) {
        client->pollMessages();
    }
    return 0;
}

// lsp_client.send(message)
static int lsp_client_send(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* msg = luaL_checkstring(L, 1);
    bool success = client->sendMessage(msg);
    lua_pushboolean(L, success ? 1 : 0);
    return 1;
}

// lsp_client.openDocument(uri, languageId, text)
static int lsp_client_open_document(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* uri = luaL_checkstring(L, 1);
    const char* languageId = luaL_checkstring(L, 2);
    const char* text = luaL_checkstring(L, 3);
    
    client->notifyDocumentOpen(uri, languageId, text);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.changeDocument(uri, startLine, startChar, endLine, endChar, newText)
static int lsp_client_change_document(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* uri = luaL_checkstring(L, 1);
    int startLine = static_cast<int>(luaL_checkinteger(L, 2));
    int startChar = static_cast<int>(luaL_checkinteger(L, 3));
    int endLine = static_cast<int>(luaL_checkinteger(L, 4));
    int endChar = static_cast<int>(luaL_checkinteger(L, 5));
    const char* newText = luaL_checkstring(L, 6);
    
    client->notifyDocumentChange(uri, startLine, startChar, endLine, endChar, newText);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.closeDocument(uri)
static int lsp_client_close_document(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* uri = luaL_checkstring(L, 1);
    client->notifyDocumentClose(uri);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.saveDocument(uri)
static int lsp_client_save_document(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* uri = luaL_checkstring(L, 1);
    client->notifyDocumentSave(uri);
    lua_pushboolean(L, 1);
    return 1;
}

// lsp_client.requestCompletion(uri, line, character, requestId)
static int lsp_client_request_completion(lua_State* L) {
    auto client = ydwe::lsp::client::getGlobalClient();
    if (!client) {
        lua_pushboolean(L, 0);
        return 1;
    }
    
    const char* uri = luaL_checkstring(L, 1);
    int line = static_cast<int>(luaL_checkinteger(L, 2));
    int character = static_cast<int>(luaL_checkinteger(L, 3));
    int requestId = static_cast<int>(luaL_checkinteger(L, 4));
    
    client->requestCompletion(uri, line, character, requestId);
    lua_pushboolean(L, 1);
    return 1;
}

static const luaL_Reg lsp_client_funcs[] = {
    {"config", lsp_client_config},
    {"setLogCallback", lsp_client_set_log_callback},
    {"setMessageCallback", lsp_client_set_message_callback},
    {"start", lsp_client_start},
    {"stop", lsp_client_stop},
    {"isRunning", lsp_client_is_running},
    {"poll", lsp_client_poll},
    {"send", lsp_client_send},
    {"openDocument", lsp_client_open_document},
    {"changeDocument", lsp_client_change_document},
    {"closeDocument", lsp_client_close_document},
    {"saveDocument", lsp_client_save_document},
    {"requestCompletion", lsp_client_request_completion},
    {NULL, NULL}
};

extern "C" __declspec(dllexport) int luaopen_lsp_client(lua_State* L) {
    luaL_newlib(L, lsp_client_funcs);
    
    // 添加常量
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "LOG_NONE");
    lua_pushinteger(L, 1);
    lua_setfield(L, -2, "LOG_ERROR");
    lua_pushinteger(L, 2);
    lua_setfield(L, -2, "LOG_WARNING");
    lua_pushinteger(L, 3);
    lua_setfield(L, -2, "LOG_INFO");
    
    return 1;
}
