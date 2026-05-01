#pragma once

// YDTrigger Agent API - C-callable functions for AI Agent via Lua FFI
// All functions use __cdecl calling convention for easy FFI consumption.

#ifdef __cplusplus
extern "C" {
#endif

#define YDT_ECA_EVENT      0
#define YDT_ECA_CONDITION  1
#define YDT_ECA_ACTION     2

// --- Trigger enumeration ---

// Refresh internal trigger cache. Must be called before reading triggers.
// Returns number of triggers found, or 0 if trigger tree is inaccessible.
__declspec(dllexport) int __cdecl ydt_refresh(void);

// Get cached trigger count
__declspec(dllexport) int __cdecl ydt_get_trigger_count(void);

// Get trigger name (pointer valid until next ydt_refresh call)
__declspec(dllexport) const char* __cdecl ydt_get_trigger_name(int trig_index);

// Returns 1 if disabled, 0 if enabled, -1 on error
__declspec(dllexport) int __cdecl ydt_get_trigger_disabled(int trig_index);

// --- ECA node access ---

// Get ECA node count for a trigger (eca_type: 0=Event, 1=Condition, 2=Action)
__declspec(dllexport) int __cdecl ydt_get_eca_count(int trig_index, int eca_type);

// Get ECA node function name
__declspec(dllexport) const char* __cdecl ydt_get_eca_func_name(int trig_index, int eca_type, int eca_idx);

// Get ECA node GUI ID (CC_GUIID_* value, for internal mapping)
__declspec(dllexport) int __cdecl ydt_get_eca_gui_id(int trig_index, int eca_type, int eca_idx);

// --- ECA parameter access ---

// Get number of parameters for an ECA node
__declspec(dllexport) int __cdecl ydt_get_eca_param_count(int trig_index, int eca_type, int eca_idx);

// Get parameter value as string (for display / AI consumption)
__declspec(dllexport) const char* __cdecl ydt_get_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx);

// --- ECA Modification API ---

// Set trigger display name. Returns 1 on success, 0 on failure.
__declspec(dllexport) int __cdecl ydt_set_trigger_name(int trig_index, const char* name);

// Enable/disable trigger. disabled=1 disables, disabled=0 enables. Returns 1 on success.
__declspec(dllexport) int __cdecl ydt_set_trigger_disabled(int trig_index, int disabled);

// Set ECA node function name (e.g. "SetVariable", "IfThenElseMultiple").
__declspec(dllexport) int __cdecl ydt_set_eca_func_name(int trig_index, int eca_type, int eca_idx, const char* name);

// Set ECA node active/inactive. active=1 enables, active=0 disables (skipped during compile).
__declspec(dllexport) int __cdecl ydt_set_eca_active(int trig_index, int eca_type, int eca_idx, int active);

// Set ECA parameter value.
__declspec(dllexport) int __cdecl ydt_set_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx, const char* value);

// Add a new ECA node (cloned from existing template of same type). Returns 1 on success.
__declspec(dllexport) int __cdecl ydt_add_eca(int trig_index, int eca_type);

// Remove an ECA node and free its memory. Returns 1 on success.
__declspec(dllexport) int __cdecl ydt_remove_eca(int trig_index, int eca_type, int eca_idx);

#ifdef __cplusplus
} // extern "C"
#endif
