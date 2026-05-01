#include <windows.h>
#include <vector>
#include <BlizzardStorm.h>
#include "Common.h"
#include "Core/CC_GUIID.h"
#include "Core/CC_VarType.h"

// Agent API: C-callable functions for AI Agent to read WE GUI trigger/ECA data.
//
// Two trigger discovery methods:
//   1. Walk the trigger tree from known WE global offsets
//   2. Capture trigger pointers during CC_Main compilation (fallback)

namespace agent_api {

static std::vector<DWORD> g_triggers;
static char g_error_buf[512] = {};

// Ring buffer for string returns (pointer stable until next call or wrap)
static char g_str_buf[8192];
static int  g_str_pos = 0;

static const char* alloc_str(const char* s) {
    if (!s) return "(null)";
    int len = (int)BLZSStrLen(s) + 1;
    if (g_str_pos + len > (int)sizeof(g_str_buf)) g_str_pos = 0;
    char* p = g_str_buf + g_str_pos;
    BLZSStrCopy(p, s, len);
    g_str_pos += len;
    return p;
}

static void set_error(const char* msg) {
    BLZSStrCopy(g_error_buf, msg, sizeof(g_error_buf));
}

// Known WE 1.27+ global offsets for the trigger tree root.
// YDWE uses the WE 1.27 binary; these are well-known offsets.
static const DWORD kKnownRootOffsets[] = {
    0x006ABF2C,
    0x006A8B58,
    0x006B012C,
    0x006AC2D0,
};

// Validate a pointer looks like a tree node (has vtable + reasonable child count)
static bool is_valid_node(DWORD ptr) {
    if (!ptr) return false;
    DWORD base = g_nWEBase;
    if (ptr < base || ptr > base + 0x02000000) return false;
    __try {
        DWORD vtable = *(DWORD*)ptr;
        if (vtable < base || vtable > base + 0x02000000) return false;
        DWORD child_count = *(DWORD*)(ptr + 0x0C);
        if (child_count > 5000) return false;
        if (child_count > 0) {
            DWORD child_array = *(DWORD*)(ptr + 0x10);
            if (child_array < base || child_array > base + 0x02000000) return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Check if a node is a trigger (has ECA-type children) vs a folder
static bool node_is_trigger(DWORD ptr) {
    __try {
        DWORD count = *(DWORD*)(ptr + 0x0C);
        if (count == 0) return false;
        DWORD first_child = *(DWORD*)(*(DWORD*)(ptr + 0x10));
        if (!first_child) return false;
        DWORD child_vtable = *(DWORD*)first_child;
        if (child_vtable < g_nWEBase || child_vtable > g_nWEBase + 0x02000000) return false;
        typedef int (_fastcall* GetTypeFn)(DWORD);
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(child_vtable + 0x08));
        if (!get_type) return false;
        int type = get_type(first_child);
        return (type >= 0 && type <= 2);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Recursively walk the trigger tree collecting trigger pointers
static void walk_tree(DWORD node) {
    if (!is_valid_node(node)) return;
    __try {
        if (node_is_trigger(node)) {
            g_triggers.push_back(node);
            return;
        }
        DWORD count = *(DWORD*)(node + 0x0C);
        DWORD* children = *(DWORD**)(node + 0x10);
        for (DWORD i = 0; i < count; i++) {
            if (children[i]) walk_tree(children[i]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Find trigger tree root via known offsets + fallback scan
static DWORD find_trigger_tree_root() {
    DWORD base = g_nWEBase;
    if (!base) return 0;

    for (int i = 0; i < sizeof(kKnownRootOffsets) / sizeof(kKnownRootOffsets[0]); i++) {
        __try {
            DWORD root = *(DWORD*)(base + kKnownRootOffsets[i]);
            if (is_valid_node(root) && *(DWORD*)(root + 0x0C) > 0) return root;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Fallback: scan WE data section for tree-like structures
    __try {
        for (DWORD addr = base + 0x006A0000; addr < base + 0x006B5000; addr += 4) {
            DWORD candidate = *(DWORD*)addr;
            if (!is_valid_node(candidate)) continue;
            DWORD child_count = *(DWORD*)(candidate + 0x0C);
            if (child_count < 1 || child_count > 500) continue;
            DWORD first_child = *(DWORD*)(*(DWORD*)(candidate + 0x10));
            if (!is_valid_node(first_child)) continue;
            return candidate;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    return 0;
}

// Count ECA nodes of a specific type for a trigger
// type: 0=Event, 1=Condition, 2=Action
static int count_eca(DWORD trig, DWORD eca_type) {
    DWORD count = *(DWORD*)(trig + 0x0C);
    DWORD* children = *(DWORD**)(trig + 0x10);
    int result = 0;
    typedef int (_fastcall* GetTypeFn)(DWORD);
    for (DWORD i = 0; i < count; i++) {
        DWORD child = children[i];
        if (!child) continue;
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
        if (!get_type) continue;
        if (get_type(child) == eca_type && *(DWORD*)(child + 0x13C) != 0) {
            result++;
        }
    }
    return result;
}

// Find the Nth active ECA node of a specific type
// Returns the node pointer, or 0 if not found
static DWORD find_eca(DWORD trig, DWORD eca_type, int eca_idx) {
    DWORD count = *(DWORD*)(trig + 0x0C);
    DWORD* children = *(DWORD**)(trig + 0x10);
    int found = 0;
    typedef int (_fastcall* GetTypeFn)(DWORD);
    for (DWORD i = 0; i < count; i++) {
        DWORD child = children[i];
        if (!child) continue;
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
        if (!get_type) continue;
        if (get_type(child) == eca_type && *(DWORD*)(child + 0x13C) != 0) {
            if (found == eca_idx) return child;
            found++;
        }
    }
    return 0;
}

} // namespace agent_api

// Called from CC_PutTrigger_Hook during compilation to build trigger list
extern "C" void agent_api_add_trigger(DWORD trigger_ptr) {
    __try {
        if (trigger_ptr) {
            agent_api::g_triggers.push_back(trigger_ptr);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

extern "C" void agent_api_clear_triggers() {
    agent_api::g_triggers.clear();
}

// ===========================================================================
// Exported C API — extern "C" for .def export and Lua FFI
// ===========================================================================

extern "C" {

int __cdecl ydt_refresh(void) {
    agent_api::g_triggers.clear();

    DWORD root = agent_api::find_trigger_tree_root();
    if (root) {
        agent_api::walk_tree(root);
    }

    if (agent_api::g_triggers.empty()) {
        agent_api::set_error("triggers not found; they will populate on next compilation");
        return 0;
    }

    agent_api::set_error("");
    return (int)agent_api::g_triggers.size();
}

int __cdecl ydt_get_trigger_count(void) {
    return (int)agent_api::g_triggers.size();
}

const char* __cdecl ydt_get_trigger_name(int trig_index) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return nullptr;
    __try {
        return alloc_str((const char*)(g_triggers[trig_index] + 0x4C));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

int __cdecl ydt_get_trigger_disabled(int trig_index) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return -1;
    __try {
        return (*(DWORD*)(g_triggers[trig_index] + 0x18)) ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int __cdecl ydt_get_eca_count(int trig_index, int eca_type) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
    __try {
        return count_eca(g_triggers[trig_index], (DWORD)eca_type);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

const char* __cdecl ydt_get_eca_func_name(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return nullptr;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node) return nullptr;
        return alloc_str((const char*)(node + 0x20));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

int __cdecl ydt_get_eca_gui_id(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return -1;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node) return -1;
        return *(int*)(node + 0x138);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

int __cdecl ydt_get_eca_param_count(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node) return 0;
        return (int)*(DWORD*)(node + 0x128);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

const char* __cdecl ydt_get_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return nullptr;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node) return nullptr;

        DWORD param_count = *(DWORD*)(node + 0x128);
        if (param_idx < 0 || param_idx >= (int)param_count) return nullptr;

        DWORD param_ptr = ((DWORD*)(*(DWORD*)(node + 0x12C)))[param_idx];
        if (!param_ptr) return alloc_str("null");

        // +0x20 holds the function/var name if it's a reference, or is empty for constants
        const char* func_name = (const char*)(param_ptr + 0x20);
        if (func_name && *func_name && BLZSStrCmp(func_name, "Null", 0x7FFFFFFF) != 0) {
            return alloc_str(func_name);
        }

        // +0x4C holds the constant value or the parameter value
        const char* raw_val = (const char*)(param_ptr + 0x4C);
        if (raw_val && BLZSStrLen(raw_val) > 0) {
            return alloc_str(raw_val);
        }

        return alloc_str("(empty)");
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// ===========================================================================
// Phase 2: ECA Modification API
// ===========================================================================

int __cdecl ydt_set_trigger_name(int trig_index, const char* name) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	if (!name) return 0;
	__try {
		BLZSStrCopy((char*)(g_triggers[trig_index] + 0x4C), name, 260);
		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_set_trigger_disabled(int trig_index, int disabled) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	__try {
		*(DWORD*)(g_triggers[trig_index] + 0x18) = disabled ? 1 : 0;
		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_set_eca_func_name(int trig_index, int eca_type, int eca_idx, const char* name) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	if (!name) return 0;
	__try {
		DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
		if (!node) return 0;
		BLZSStrCopy((char*)(node + 0x20), name, 260);
		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_set_eca_active(int trig_index, int eca_type, int eca_idx, int active) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	__try {
		DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
		if (!node) return 0;
		*(DWORD*)(node + 0x13C) = active ? 1 : 0;
		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_set_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx, const char* value) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	if (!value) return 0;
	__try {
		DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
		if (!node) return 0;

		DWORD param_count = *(DWORD*)(node + 0x128);
		if (param_idx < 0 || param_idx >= (int)param_count) return 0;

		DWORD param_ptr = ((DWORD*)(*(DWORD*)(node + 0x12C)))[param_idx];
		if (!param_ptr) return 0;

		BLZSStrCopy((char*)(param_ptr + 0x4C), value, 260);
		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int add_eca_to_trigger(DWORD trig, DWORD eca_type, DWORD source_node) {
	__try {
		DWORD old_count = *(DWORD*)(trig + 0x0C);
		DWORD* old_array = *(DWORD**)(trig + 0x10);

		const DWORD NODE_SIZE = 0x200;

		DWORD new_node = (DWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NODE_SIZE);
		if (!new_node) return 0;

		memcpy((void*)new_node, (void*)source_node, NODE_SIZE);

		*(DWORD*)(new_node + 0x13C) = 1;

		DWORD* new_array = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (old_count + 1) * sizeof(DWORD));
		if (!new_array) {
			HeapFree(GetProcessHeap(), 0, (void*)new_node);
			return 0;
		}

		if (old_count > 0 && old_array) {
			memcpy(new_array, old_array, old_count * sizeof(DWORD));
		}
		new_array[old_count] = new_node;

		*(DWORD*)(trig + 0x0C) = old_count + 1;
		*(DWORD*)(trig + 0x10) = (DWORD)new_array;

		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_add_eca(int trig_index, int eca_type) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	if (eca_type < 0 || eca_type > 2) return 0;

	__try {
		DWORD trig = g_triggers[trig_index];

		DWORD template_node = 0;
		{
			DWORD count = *(DWORD*)(trig + 0x0C);
			DWORD* children = *(DWORD**)(trig + 0x10);
			typedef int (_fastcall* GetTypeFn)(DWORD);

			for (DWORD i = 0; i < count; i++) {
				DWORD child = children[i];
				if (!child) continue;
				GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
				if (!get_type) continue;
				if (get_type(child) == (int)eca_type) {
					template_node = child;
					break;
				}
			}
		}

		if (!template_node) {
			for (size_t t = 0; t < g_triggers.size(); t++) {
				if ((int)t == trig_index) continue;
				DWORD t_trig = g_triggers[t];
				DWORD count = *(DWORD*)(t_trig + 0x0C);
				DWORD* children = *(DWORD**)(t_trig + 0x10);
				typedef int (_fastcall* GetTypeFn)(DWORD);

				for (DWORD i = 0; i < count; i++) {
					DWORD child = children[i];
					if (!child) continue;
					GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
					if (!get_type) continue;
					if (get_type(child) == (int)eca_type) {
						template_node = child;
						break;
					}
				}
				if (template_node) break;
			}
		}

		if (!template_node) return 0;

		return add_eca_to_trigger(trig, (DWORD)eca_type, template_node);
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_remove_eca(int trig_index, int eca_type, int eca_idx) {
	using namespace agent_api;
	if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;
	__try {
		DWORD trig = g_triggers[trig_index];
		DWORD count = *(DWORD*)(trig + 0x0C);
		DWORD* children = *(DWORD**)(trig + 0x10);

		typedef int (_fastcall* GetTypeFn)(DWORD);
		int found_idx = -1;
		DWORD found_node = 0;
		int eca_found = 0;

		for (DWORD i = 0; i < count; i++) {
			DWORD child = children[i];
			if (!child) continue;
			GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
			if (!get_type) continue;
			if (get_type(child) == eca_type) {
				if (eca_found == eca_idx) {
					found_idx = i;
					found_node = child;
					break;
				}
				eca_found++;
			}
		}

		if (found_idx < 0 || !found_node) return 0;

		{
			DWORD pcount = *(DWORD*)(found_node + 0x128);
			DWORD* parray = *(DWORD**)(found_node + 0x12C);
			if (parray) {
				for (DWORD p = 0; p < pcount; p++) {
					if (parray[p]) {
						HeapFree(GetProcessHeap(), 0, (void*)parray[p]);
					}
				}
				HeapFree(GetProcessHeap(), 0, (void*)parray);
			}

			DWORD ccount = *(DWORD*)(found_node + 0x0C);
			DWORD* carray = *(DWORD**)(found_node + 0x10);
			if (carray) {
				for (DWORD c = 0; c < ccount; c++) {
					if (carray[c]) {
						DWORD nested_pcount = *(DWORD*)(carray[c] + 0x128);
						DWORD* nested_parray = *(DWORD**)(carray[c] + 0x12C);
						if (nested_parray) {
							for (DWORD np = 0; np < nested_pcount; np++) {
								if (nested_parray[np])
									HeapFree(GetProcessHeap(), 0, (void*)nested_parray[np]);
							}
							HeapFree(GetProcessHeap(), 0, (void*)nested_parray);
						}
						HeapFree(GetProcessHeap(), 0, (void*)carray[c]);
					}
				}
				HeapFree(GetProcessHeap(), 0, (void*)carray);
			}
		}

		HeapFree(GetProcessHeap(), 0, (void*)found_node);

		if (count > 1) {
			DWORD* new_array = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (count - 1) * sizeof(DWORD));
			if (new_array) {
				DWORD dest = 0;
				for (DWORD i = 0; i < count; i++) {
					if ((int)i != found_idx) {
						new_array[dest++] = children[i];
					}
				}
				*(DWORD*)(trig + 0x0C) = count - 1;
				*(DWORD*)(trig + 0x10) = (DWORD)new_array;
				HeapFree(GetProcessHeap(), 0, (void*)children);
			}
		} else {
			*(DWORD*)(trig + 0x0C) = 0;
			*(DWORD*)(trig + 0x10) = 0;
			HeapFree(GetProcessHeap(), 0, (void*)children);
		}

		return 1;
	} __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

} // extern "C"
