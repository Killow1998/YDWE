#include "catch2/catch_amalgamated.hpp"
#include <windows.h>
#include <string>
#include <vector>
#include <BlizzardStorm.h>
#include "Core/CC_VarType.h"

extern "C" {
int agent_api_capture_globals_container(DWORD container);
void agent_api_capture_global_name(DWORD index, const char* name, DWORD raw_type, const char* type_name, DWORD context);
int ydt_get_global_count(void);
const char* ydt_get_global_name(int index);
int ydt_get_global_type(int index);
const char* ydt_get_global_value(int index);
int ydt_set_global_value(int index, const char* value);
const char* ydt_global_diag(void);
}

TEST_CASE("AgentAPI global variables use mock WE container layout", "[agent][globals]") {
    const DWORD entry_stride = 0x1C0;
    const DWORD name_offset = 0x2E;
    const DWORD count = 2;
    const size_t container_size = 0x20;
    const size_t entries_size = entry_stride * count;
    std::vector<unsigned char> memory(container_size + entries_size, 0);

    DWORD container = reinterpret_cast<DWORD>(memory.data());
    DWORD entries = reinterpret_cast<DWORD>(memory.data() + container_size);
    *reinterpret_cast<DWORD*>(container + 0x08) = count;
    *reinterpret_cast<DWORD*>(container + 0x0C) = entries;

    BLZSStrCopy(reinterpret_cast<char*>(entries + name_offset), "udg_MockInt", 260);
    BLZSStrCopy(reinterpret_cast<char*>(entries + entry_stride + name_offset), "udg_MockUnitArray", 260);

    REQUIRE(agent_api_capture_globals_container(0) == 0);
    REQUIRE(agent_api_capture_globals_container(container) == 1);

    REQUIRE(ydt_get_global_count() == 2);
    REQUIRE(std::string(ydt_get_global_name(0)) == "udg_MockInt");
    REQUIRE(std::string(ydt_get_global_name(1)) == "udg_MockUnitArray");
    REQUIRE(ydt_get_global_name(-1) == nullptr);
    REQUIRE(ydt_get_global_name(2) == nullptr);

    REQUIRE(ydt_get_global_type(0) == -1);
    REQUIRE(std::string(ydt_get_global_value(0)) == "");
    REQUIRE(ydt_set_global_value(0, "123") == 0);
    REQUIRE(ydt_set_global_value(99, "123") == 0);

    agent_api_capture_global_name(0, "udg_MockInt", CC_VARTYPE_integer, "integer", container);
    std::string diag = ydt_global_diag();
    REQUIRE(diag.find("\"last_name\":\"udg_MockInt\"") != std::string::npos);
    REQUIRE(diag.find("\"last_type_name\":\"integer\"") != std::string::npos);
    REQUIRE(diag.find("\"stride_name\":\"udg_MockInt\"") != std::string::npos);
    REQUIRE(diag.find("\"alt_count\":2") != std::string::npos);
}
