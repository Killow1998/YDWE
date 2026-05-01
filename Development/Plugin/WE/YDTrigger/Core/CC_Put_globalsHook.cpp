#include "CC_Include.h"

// Forward: capture global variable container (defined in AgentAPI.cpp)
extern "C" void agent_api_capture_globals_container(DWORD container);

void _fastcall
    CC_Put_globals_Hook(DWORD OutClass)
{
	CC_PutString(OutClass, 0, "#define USE_BJ_ANTI_LEAK", 1);
    CC_PutString(OutClass, 0, "#include <YDTrigger/Import.h>", 1);
    CC_PutString(OutClass, 0, "#include <YDTrigger/YDTrigger.h>", 1);
    CC_PutString(OutClass, 0, "globals", 1);
    ((DWORD*)OutClass)[3]++;

    // Capture globals container via GetGlobalVarName's This parameter
    // We call GetGlobalVarName(0) to get name of first var; the This container
    // is obtained from a scan or passed via hook.
    // For now, scan for container near data section.
    extern DWORD g_nWEBase;
    DWORD base = g_nWEBase;
    __try {
        for (DWORD addr = base + 0x00600000; addr < base + 0x00800000; addr += 4) {
            DWORD cand = *(DWORD*)addr;
            if (cand < base || cand > base + 0x03000000) continue;
            DWORD vc = *(DWORD*)(cand + 0x128);
            if (vc < 1 || vc > 5000) continue;
            DWORD* va = *(DWORD**)(cand + 0x12C);
            if (!va) continue;
            DWORD v0 = va[0];
            if (!v0 || v0 < base || v0 > base + 0x03000000) continue;
            // Found candidate globals container
            agent_api_capture_globals_container(cand);
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
