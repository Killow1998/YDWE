#include "Common.h"

BOOL CreateYDTriggerImportFile();

extern BOOL g_bDisableSaveLoadSystem;

extern "C" void agent_api_clear_triggers();

void _fastcall
CC_Main_Hook(DWORD OutClass)
{
	g_bDisableSaveLoadSystem = TRUE;
	agent_api_clear_triggers();
	CC_Main(OutClass);

	CreateYDTriggerImportFile();
}
