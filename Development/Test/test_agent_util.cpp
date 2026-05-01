// test_agent_util.cpp — AgentAPI utility function tests
//
// Tests the ring buffer allocator, string utilities, and trigger cache
// management. Trigger/ECA memory scanning functions are NOT tested here
// (they require the WE process).
//
// AgentAPI.cpp depends on g_nWEBase from Common.cpp. We provide a stub
// here so the translation unit can link without compiling all of Common.cpp.

#include "catch2/catch_amalgamated.hpp"
#include <windows.h>
#include <vector>
#include <BlizzardStorm.h>

// Stub for AgentAPI's dependency on Common.cpp
DWORD g_nWEBase = 0x00400000;

// C exports from AgentAPI
extern "C" {
void agent_api_add_trigger(DWORD trigger_ptr);
void agent_api_clear_triggers();
int  ydt_get_trigger_count(void);
const char* ydt_get_trigger_name(int trig_index);
int  ydt_get_eca_count(int trig_index, int eca_type);
const char* ydt_get_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx);
}

// Access the ring buffer for testing
// The ring buffer is static inside AgentAPI namespace — we test via the
// exported functions that return pointers into it.

TEST_CASE("AgentAPI — trigger cache management", "[agent][cache]") {
    agent_api_clear_triggers();

    // Initially zero triggers
    REQUIRE(ydt_get_trigger_count() == 0);

    // Add a mock trigger pointer (just a heap block — no WE memory needed
    // since we're only testing the cache, not the actual memory reads)
    DWORD* mock = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x200);
    REQUIRE(mock != nullptr);

    // Set up minimal trigger-like structure for name access
    const char* test_name = "TestTrigger";
    mock[0x4C / 4] = (DWORD)test_name; // offset +0x4C = name pointer (simulated)
    mock[0x0C / 4] = 0; // child count = 0

    agent_api_add_trigger((DWORD)mock);
    REQUIRE(ydt_get_trigger_count() == 1);

    // Add another
    agent_api_add_trigger((DWORD)mock);
    REQUIRE(ydt_get_trigger_count() == 2);

    // Clear
    agent_api_clear_triggers();
    REQUIRE(ydt_get_trigger_count() == 0);

    HeapFree(GetProcessHeap(), 0, mock);
}

TEST_CASE("AgentAPI — trigger name access with valid pointer", "[agent][name]") {
    agent_api_clear_triggers();

    // Create a mock trigger with proper name string at offset +0x4C
    DWORD* mock = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x200);
    REQUIRE(mock != nullptr);

    // Put the name string at +0x4C
    char name_buf[260];
    BLZSStrCopy(name_buf, "MyTestTrigger", 260);
    mock[0x4C / 4] = (DWORD)(char*)name_buf; // ~approximate — this test
    // can't really work since +0x4C should point to a string in WE memory.
    // For the real test, the data at +0x4C is treated as inline char[260],
    // not a pointer. Let's just test the cache behavior.

    // Actually looking at AgentAPI.cpp line 214:
    // return alloc_str((const char*)(g_triggers[trig_index] + 0x4C));
    // This treats offset +0x4C as inline char data (char[260]).
    // So we just put the name directly at +0x4C in our mock:
    BLZSStrCopy((char*)(mock + 0x4C/4), "MyTestTrigger", 260);

    agent_api_add_trigger((DWORD)mock);
    REQUIRE(ydt_get_trigger_count() == 1);

    // Read back name — this will work because we wrote the string
    // directly into the mock buffer at the correct offset
    const char* name = ydt_get_trigger_name(0);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == "MyTestTrigger");

    agent_api_clear_triggers();
    HeapFree(GetProcessHeap(), 0, mock);
}

TEST_CASE("AgentAPI — ring buffer handles long strings", "[agent][ringbuf]") {
    agent_api_clear_triggers();

    // Create a mock trigger with a long name
    DWORD* mock = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x200);
    REQUIRE(mock != nullptr);

    // Write a 255-char name (max for trigger name)
    std::string long_name(255, 'A');
    BLZSStrCopy((char*)(mock + 0x4C/4), long_name.c_str(), 260);

    agent_api_add_trigger((DWORD)mock);

    const char* name = ydt_get_trigger_name(0);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == long_name);

    agent_api_clear_triggers();
    HeapFree(GetProcessHeap(), 0, mock);
}

TEST_CASE("AgentAPI — ring buffer wraps correctly", "[agent][ringbuf]") {
    agent_api_clear_triggers();
    const int RING_SIZE = 8192;

    // Create 30 triggers with 256-char names (30 * 257 = 7710 < 8192)
    // then create more to force wrapping
    std::vector<DWORD*> mocks;
    std::vector<std::string> names;

    for (int i = 0; i < 35; i++) {
        DWORD* m = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x200);
        REQUIRE(m != nullptr);
        char name[260];
        BLZSStrPrintf(name, 260, "Trigger_%03d_", i);
        int len = (int)BLZSStrLen(name);
        // Fill remaining with 'X' to make ~250 bytes
        for (int j = len; j < 250; j++) name[j] = 'X';
        name[250] = '\0';
        BLZSStrCopy((char*)(m + 0x4C / 4), name, 260);
        agent_api_add_trigger((DWORD)m);
        mocks.push_back(m);
        names.push_back(std::string(name));
    }

    // Read back all triggers — the ring buffer should have wrapped
    // but each name should still be correct
    int count = ydt_get_trigger_count();
    REQUIRE(count == 35);

    for (int i = 0; i < count; i++) {
        const char* got = ydt_get_trigger_name(i);
        REQUIRE(got != nullptr);
        REQUIRE(std::string(got) == names[i]);
    }

    // Cleanup
    agent_api_clear_triggers();
    for (auto* m : mocks) HeapFree(GetProcessHeap(), 0, m);
}

TEST_CASE("AgentAPI — out-of-bounds access returns safe values", "[agent][bounds]") {
    agent_api_clear_triggers();

    // Negative index
    REQUIRE(ydt_get_trigger_name(-1) == nullptr);
    REQUIRE(ydt_get_trigger_name(9999) == nullptr);
    REQUIRE(ydt_get_eca_count(-1, 0) == 0);
    REQUIRE(ydt_get_eca_count(0, 0) == 0);
    REQUIRE(ydt_get_eca_param_value(0, 0, 0, 0) == nullptr);
}

TEST_CASE("AgentAPI — add trigger NULL safety", "[agent][null]") {
    agent_api_clear_triggers();

    // Adding NULL should not crash or add
    agent_api_add_trigger(0);
    REQUIRE(ydt_get_trigger_count() == 0);

    // Adding valid pointer after NULL should still work
    DWORD* mock = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x200);
    REQUIRE(mock != nullptr);
    BLZSStrCopy((char*)(mock + 0x4C / 4), "AfterNull", 260);

    agent_api_add_trigger((DWORD)mock);
    REQUIRE(ydt_get_trigger_count() == 1);

    const char* name = ydt_get_trigger_name(0);
    REQUIRE(name != nullptr);
    REQUIRE(std::string(name) == "AfterNull");

    agent_api_clear_triggers();
    HeapFree(GetProcessHeap(), 0, mock);
}
