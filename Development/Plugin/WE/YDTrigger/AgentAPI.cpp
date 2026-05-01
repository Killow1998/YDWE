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
static int g_str_pos = 0;

static const char* alloc_str(const char* s) {
    if (!s)
        return "(null)";
    int len = (int)BLZSStrLen(s) + 1;
    if (g_str_pos + len > (int)sizeof(g_str_buf))
        g_str_pos = 0;
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
    if (!ptr)
        return false;
    DWORD base = g_nWEBase;
    if (ptr < base || ptr > base + 0x02000000)
        return false;
    __try {
        DWORD vtable = *(DWORD*)ptr;
        if (vtable < base || vtable > base + 0x02000000)
            return false;
        DWORD child_count = *(DWORD*)(ptr + 0x0C);
        if (child_count > 5000)
            return false;
        if (child_count > 0) {
            DWORD child_array = *(DWORD*)(ptr + 0x10);
            if (child_array < base || child_array > base + 0x02000000)
                return false;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Check if a node is a trigger (has ECA-type children) vs a folder
static bool node_is_trigger(DWORD ptr) {
    __try {
        DWORD count = *(DWORD*)(ptr + 0x0C);
        if (count == 0)
            return false;
        DWORD first_child = *(DWORD*)(*(DWORD*)(ptr + 0x10));
        if (!first_child)
            return false;
        DWORD child_vtable = *(DWORD*)first_child;
        if (child_vtable < g_nWEBase || child_vtable > g_nWEBase + 0x02000000)
            return false;
        typedef int(_fastcall * GetTypeFn)(DWORD);
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(child_vtable + 0x08));
        if (!get_type)
            return false;
        int type = get_type(first_child);
        return (type >= 0 && type <= 2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Recursively walk the trigger tree collecting trigger pointers
static void walk_tree(DWORD node) {
    if (!is_valid_node(node))
        return;
    __try {
        if (node_is_trigger(node)) {
            g_triggers.push_back(node);
            return;
        }
        DWORD count = *(DWORD*)(node + 0x0C);
        DWORD* children = *(DWORD**)(node + 0x10);
        for (DWORD i = 0; i < count; i++) {
            if (children[i])
                walk_tree(children[i]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Find trigger tree root via known offsets + fallback scan
static DWORD find_trigger_tree_root() {
    DWORD base = g_nWEBase;
    if (!base)
        return 0;

    for (int i = 0; i < sizeof(kKnownRootOffsets) / sizeof(kKnownRootOffsets[0]); i++) {
        __try {
            DWORD root = *(DWORD*)(base + kKnownRootOffsets[i]);
            if (is_valid_node(root) && *(DWORD*)(root + 0x0C) > 0)
                return root;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // Fallback: scan WE data section for tree-like structures
    __try {
        for (DWORD addr = base + 0x006A0000; addr < base + 0x006B5000; addr += 4) {
            DWORD candidate = *(DWORD*)addr;
            if (!is_valid_node(candidate))
                continue;
            DWORD child_count = *(DWORD*)(candidate + 0x0C);
            if (child_count < 1 || child_count > 500)
                continue;
            DWORD first_child = *(DWORD*)(*(DWORD*)(candidate + 0x10));
            if (!is_valid_node(first_child))
                continue;
            return candidate;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }

    return 0;
}

// Count ECA nodes of a specific type for a trigger
// type: 0=Event, 1=Condition, 2=Action
static int count_eca(DWORD trig, DWORD eca_type) {
    DWORD count = *(DWORD*)(trig + 0x0C);
    DWORD* children = *(DWORD**)(trig + 0x10);
    int result = 0;
    typedef int(_fastcall * GetTypeFn)(DWORD);
    for (DWORD i = 0; i < count; i++) {
        DWORD child = children[i];
        if (!child)
            continue;
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
        if (!get_type)
            continue;
        if (get_type(child) == eca_type && *(DWORD*)(child + 0x13C) != 0) {
            result++;
        }
    }
    return result;
}

// Find the Nth ECA node of a specific type (active_only=true skips inactive nodes)
static DWORD find_eca_ex(DWORD trig, DWORD eca_type, int eca_idx, bool active_only) {
    DWORD count = *(DWORD*)(trig + 0x0C);
    DWORD* children = *(DWORD**)(trig + 0x10);
    int found = 0;
    typedef int(_fastcall * GetTypeFn)(DWORD);
    for (DWORD i = 0; i < count; i++) {
        DWORD child = children[i];
        if (!child)
            continue;
        GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
        if (!get_type)
            continue;
        if (get_type(child) == eca_type) {
            if (active_only && *(DWORD*)(child + 0x13C) == 0)
                continue;
            if (found == eca_idx)
                return child;
            found++;
        }
    }
    return 0;
}
static DWORD find_eca(DWORD trig, DWORD eca_type, int eca_idx) {
    return find_eca_ex(trig, eca_type, eca_idx, true);
}

// Scan memory for a node whose child array contains 'child_ptr'.
// Searches WE data section heuristically for performance.
static DWORD find_parent_node(DWORD child_ptr, DWORD base) {
    if (!child_ptr || !base) return 0;
    __try {
        // Scan within a reasonable range around where WE allocates tree nodes.
        // WE uses its own heap allocator; nodes typically reside in 0x00A00000-0x02000000 range.
        for (DWORD addr = base + 0x00800000; addr < base + 0x03000000; addr += 4) {
            DWORD candidate = *(DWORD*)addr;
            if (!is_valid_node(candidate)) continue;
            DWORD cc = *(DWORD*)(candidate + 0x0C);
            if (cc < 1 || cc > 1000) continue;
            DWORD* carr = *(DWORD**)(candidate + 0x10);
            if (!carr) continue;
            // Safe read: check first and last child pointer validity
            if (!is_valid_node(carr[0])) continue;
            // Search children for our trigger
            for (DWORD j = 0; j < cc && j < 200; j++) {
                if (carr[j] == child_ptr) return candidate;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

// Find tree root by walking up from a captured trigger's parent chain
static DWORD find_root_from_triggers() {
    DWORD base = g_nWEBase;
    if (!base || g_triggers.empty()) return 0;

    for (size_t i = 0; i < g_triggers.size(); i++) {
        DWORD trig = g_triggers[i];
        // Find this trigger's parent
        DWORD parent = find_parent_node(trig, base);
        if (!parent) continue;

        // Walk up the parent chain to find root
        DWORD root = parent;
        for (int depth = 0; depth < 10; depth++) {
            DWORD grandparent = find_parent_node(root, base);
            if (!grandparent) break; // root reached (node with no parent)
            root = grandparent;
        }
        if (is_valid_node(root) && *(DWORD*)(root + 0x0C) > 0) {
            return root;
        }
    }
    return 0;
}

// ===== Global variable support =====

static DWORD g_globals_container = 0;

static DWORD find_global_var_container() {
    DWORD base = g_nWEBase;
    if (!base) return 0;
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
            g_globals_container = cand;
            return cand;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return 0;
}

extern "C" void agent_api_capture_globals_container(DWORD container) {
    if (container) g_globals_container = container;
}

struct GlobalVar {
    char name[260];
    DWORD type;
    char value[260];
};
static std::vector<GlobalVar> g_globals;

static void refresh_globals() {
    g_globals.clear();
    if (!g_globals_container) return;
    DWORD base = g_nWEBase;
    __try {
        DWORD count = *(DWORD*)(g_globals_container + 0x128);
        DWORD* varray = *(DWORD**)(g_globals_container + 0x12C);
        if (!varray || count > 5000) return;
        for (DWORD i = 0; i < count; i++) {
            DWORD vp = varray[i];
            if (!vp) continue;
            GlobalVar gv = {};
            // Get name by calling GetGlobalVarName if available, else use index
            // The name is sometimes stored at +0x20 or +0x4C of the container's children
            const char* nm = (const char*)(vp + 0x20);
            if (nm && *nm) BLZSStrCopy(gv.name, nm, 260);
            else {
                BLZSStrPrintf(gv.name, 260, "var_%03d", i);
            }
            gv.type = *(DWORD*)(vp + 0x48);
            const char* val = (const char*)(vp + 0x4C);
            if (val) BLZSStrCopy(gv.value, val, 260);
            g_globals.push_back(gv);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

} // namespace agent_api

// Called from CC_PutTrigger_Hook during compilation to build trigger list
extern "C" void agent_api_add_trigger(DWORD trigger_ptr) {
    __try {
        if (trigger_ptr) {
            agent_api::g_triggers.push_back(trigger_ptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
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
    if (!root) root = agent_api::find_root_from_triggers();
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
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return nullptr;
    __try {
        return alloc_str((const char*)(g_triggers[trig_index] + 0x4C));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int __cdecl ydt_get_trigger_disabled(int trig_index) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return -1;
    __try {
        return (*(DWORD*)(g_triggers[trig_index] + 0x18)) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int __cdecl ydt_get_eca_count(int trig_index, int eca_type) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    __try {
        return count_eca(g_triggers[trig_index], (DWORD)eca_type);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* __cdecl ydt_get_eca_func_name(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return nullptr;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return nullptr;
        return alloc_str((const char*)(node + 0x20));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int __cdecl ydt_get_eca_gui_id(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return -1;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return -1;
        return *(int*)(node + 0x138);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int __cdecl ydt_get_eca_param_count(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return 0;
        return (int)*(DWORD*)(node + 0x128);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

const char* __cdecl ydt_get_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return nullptr;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return nullptr;

        DWORD param_count = *(DWORD*)(node + 0x128);
        if (param_idx < 0 || param_idx >= (int)param_count)
            return nullptr;

        DWORD param_ptr = ((DWORD*)(*(DWORD*)(node + 0x12C)))[param_idx];
        if (!param_ptr)
            return alloc_str("null");

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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// ===========================================================================
// Phase 2: ECA Modification API
// ===========================================================================

int __cdecl ydt_set_trigger_name(int trig_index, const char* name) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    if (!name)
        return 0;
    __try {
        BLZSStrCopy((char*)(g_triggers[trig_index] + 0x4C), name, 260);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_set_trigger_disabled(int trig_index, int disabled) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    __try {
        DWORD val = disabled ? 1 : 0;
        *(DWORD*)(g_triggers[trig_index] + 0x18) = val; // InitTrig name skip

        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_set_eca_func_name(int trig_index, int eca_type, int eca_idx, const char* name) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    if (!name)
        return 0;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return 0;
        BLZSStrCopy((char*)(node + 0x20), name, 260);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_set_eca_active(int trig_index, int eca_type, int eca_idx, int active) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    __try {
        DWORD node = find_eca_ex(g_triggers[trig_index], (DWORD)eca_type, eca_idx, false);
        if (!node)
            return 0;
        *(DWORD*)(node + 0x13C) = active ? 1 : 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_set_eca_param_value(int trig_index, int eca_type, int eca_idx, int param_idx, const char* value) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    if (!value)
        return 0;
    __try {
        DWORD node = find_eca(g_triggers[trig_index], (DWORD)eca_type, eca_idx);
        if (!node)
            return 0;

        DWORD param_count = *(DWORD*)(node + 0x128);
        if (param_idx < 0 || param_idx >= (int)param_count)
            return 0;

        DWORD param_ptr = ((DWORD*)(*(DWORD*)(node + 0x12C)))[param_idx];
        if (!param_ptr)
            return 0;

        BLZSStrCopy((char*)(param_ptr + 0x4C), value, 260);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Find a CommentString node to clone from (looks for func name matching "CommentString")
static DWORD find_comment_string_template() {
    using namespace agent_api;
    for (size_t t = 0; t < g_triggers.size(); t++) {
        DWORD trig = g_triggers[t];
        DWORD count = *(DWORD*)(trig + 0x0C);
        DWORD* children = *(DWORD**)(trig + 0x10);
        for (DWORD i = 0; i < count; i++) {
            DWORD child = children[i];
            if (!child) continue;
            const char* fn = (const char*)(child + 0x20);
            if (fn && BLZSStrCmp(fn, "CommentString", 0x7FFFFFFF) == 0 && *(DWORD*)(child + 0x13C) != 0) {
                return child;
            }
        }
    }
    return 0;
}

static int add_eca_to_trigger(DWORD trig, DWORD eca_type, DWORD source_node) {
    __try {
        DWORD old_count = *(DWORD*)(trig + 0x0C);
        DWORD* old_array = *(DWORD**)(trig + 0x10);

        const DWORD NODE_SIZE = 0x200;
        const DWORD PARAM_SIZE = 0x200;

        // Prefer CommentString node for safe clone (deep-copies params with valid vtables)
        DWORD cs_node = find_comment_string_template();
        DWORD tpl_node = cs_node ? cs_node : source_node;

        DWORD new_node = (DWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, NODE_SIZE);
        if (!new_node)
            return 0;

        memcpy((void*)new_node, (void*)tpl_node, NODE_SIZE);

        // Deep-copy parameter array
        DWORD src_pcount = *(DWORD*)(new_node + 0x128);
        DWORD* src_parray = *(DWORD**)(new_node + 0x12C);
        if (src_pcount > 0 && src_parray) {
            DWORD* new_params = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, src_pcount * sizeof(DWORD));
            if (new_params) {
                for (DWORD p = 0; p < src_pcount; p++) {
                    DWORD sp = src_parray[p];
                    DWORD np = (DWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, PARAM_SIZE);
                    if (np) {
                        memcpy((void*)np, (void*)sp, PARAM_SIZE);
                        new_params[p] = np;
                    }
                }
                *(DWORD*)(new_node + 0x12C) = (DWORD)new_params;
            }
        }
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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_add_eca(int trig_index, int eca_type) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    if (eca_type < 0 || eca_type > 2)
        return 0;

    __try {
        DWORD trig = g_triggers[trig_index];

        DWORD template_node = 0;
        {
            DWORD count = *(DWORD*)(trig + 0x0C);
            DWORD* children = *(DWORD**)(trig + 0x10);
            typedef int(_fastcall * GetTypeFn)(DWORD);

            for (DWORD i = 0; i < count; i++) {
                DWORD child = children[i];
                if (!child)
                    continue;
                GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
                if (!get_type)
                    continue;
                if (get_type(child) == (int)eca_type) {
                    template_node = child;
                    break;
                }
            }
        }

        if (!template_node) {
            for (size_t t = 0; t < g_triggers.size(); t++) {
                if ((int)t == trig_index)
                    continue;
                DWORD t_trig = g_triggers[t];
                DWORD count = *(DWORD*)(t_trig + 0x0C);
                DWORD* children = *(DWORD**)(t_trig + 0x10);
                typedef int(_fastcall * GetTypeFn)(DWORD);

                for (DWORD i = 0; i < count; i++) {
                    DWORD child = children[i];
                    if (!child)
                        continue;
                    GetTypeFn get_type = (GetTypeFn)(*(DWORD*)(*(DWORD*)child + 0x08));
                    if (!get_type)
                        continue;
                    if (get_type(child) == (int)eca_type) {
                        template_node = child;
                        break;
                    }
                }
                if (template_node)
                    break;
            }
        }

        if (!template_node)
            return 0;

        return add_eca_to_trigger(trig, (DWORD)eca_type, template_node);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_remove_eca(int trig_index, int eca_type, int eca_idx) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size())
        return 0;
    __try {
        DWORD trig = g_triggers[trig_index];
        DWORD count = *(DWORD*)(trig + 0x0C);
        DWORD* children = *(DWORD**)(trig + 0x10);

        DWORD found_node = find_eca_ex(trig, eca_type, eca_idx, false);
        if (!found_node)
            return 0;
        int found_idx = -1;
        for (DWORD i = 0; i < count; i++) {
            if (children[i] == found_node) { found_idx = (int)i; break; }
        }

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
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

int __cdecl ydt_create_trigger(const char* name) {
    if (!name)
        return 0;

    __try {
        // Find a template trigger to clone
        if (agent_api::g_triggers.empty())
            return 0;
        DWORD tpl = agent_api::g_triggers[0];

        const DWORD TRIG_SIZE = 0x300;
        DWORD new_trig = (DWORD)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, TRIG_SIZE);
        if (!new_trig)
            return 0;
        memcpy((void*)new_trig, (void*)tpl, TRIG_SIZE);

        // Set name, zero children, mark as not disabled
        BLZSStrCopy((char*)(new_trig + 0x4C), name, 260);
        *(DWORD*)(new_trig + 0x0C) = 0;  // child count
        *(DWORD*)(new_trig + 0x10) = 0;  // child array
        *(DWORD*)(new_trig + 0x18) = 0;  // not disabled
        *(DWORD*)(new_trig + 0x24) = 0;  // no disable register

        // Find tree root (try known offsets, then parent-chain from triggers)
        DWORD root = agent_api::find_trigger_tree_root();
        if (!root) root = agent_api::find_root_from_triggers();

        if (root) {
            // Add new trigger as child of root
            DWORD old_count = *(DWORD*)(root + 0x0C);
            DWORD* old_array = *(DWORD**)(root + 0x10);
            DWORD* new_array = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (old_count + 1) * sizeof(DWORD));
            if (new_array) {
                if (old_count > 0 && old_array)
                    memcpy(new_array, old_array, old_count * sizeof(DWORD));
                new_array[old_count] = new_trig;
                *(DWORD*)(root + 0x0C) = old_count + 1;
                *(DWORD*)(root + 0x10) = (DWORD)new_array;
            }
        }

        // Always add to g_triggers so it's accessible via API
        agent_api::g_triggers.push_back(new_trig);
        return root ? 1 : 0; // 1=added to tree, 0=added to cache only
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// SEH-safe helper for delete_trigger (no C++ objects)
static int delete_trigger_impl(DWORD trig, DWORD root) {
    __try {
        // Remove from root's children
        if (root) {
            DWORD pcount = *(DWORD*)(root + 0x0C);
            DWORD* parray = *(DWORD**)(root + 0x10);
            if (parray) {
                for (DWORD i = 0; i < pcount; i++) {
                    if (parray[i] == trig) {
                        if (pcount > 1) {
                            DWORD* na = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (pcount-1)*sizeof(DWORD));
                            if (na) {
                                DWORD d=0; for(DWORD j=0;j<pcount;j++) if(j!=i) na[d++]=parray[j];
                                *(DWORD*)(root+0x0C)=pcount-1; *(DWORD*)(root+0x10)=(DWORD)na;
                            }
                        } else { *(DWORD*)(root+0x0C)=0; *(DWORD*)(root+0x10)=0; }
                        break;
                    }
                }
            }
        }
        // Free ECA children
        DWORD tc = *(DWORD*)(trig + 0x0C); DWORD* ta = *(DWORD**)(trig + 0x10);
        if (ta) { for(DWORD i=0;i<tc;i++) if(ta[i]) HeapFree(GetProcessHeap(),0,(void*)ta[i]); HeapFree(GetProcessHeap(),0,(void*)ta); }
        HeapFree(GetProcessHeap(), 0, (void*)trig);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_get_global_count(void) {
    using namespace agent_api;
    if (!g_globals_container) { refresh_globals(); find_global_var_container(); refresh_globals(); }
    if (g_globals.empty()) refresh_globals();
    return (int)g_globals.size();
}

const char* __cdecl ydt_get_global_name(int index) {
    using namespace agent_api;
    if (index < 0 || index >= (int)g_globals.size()) return nullptr;
    return alloc_str(g_globals[index].name);
}

int __cdecl ydt_get_global_type(int index) {
    using namespace agent_api;
    if (index < 0 || index >= (int)g_globals.size()) return -1;
    return (int)g_globals[index].type;
}

const char* __cdecl ydt_get_global_value(int index) {
    using namespace agent_api;
    if (index < 0 || index >= (int)g_globals.size()) return nullptr;
    return alloc_str(g_globals[index].value);
}

int __cdecl ydt_set_global_value(int index, const char* value) {
    using namespace agent_api;
    if (index < 0 || index >= (int)g_globals.size() || !value) return 0;
    __try {
        BLZSStrCopy(g_globals[index].value, value, 260);
        if (g_globals_container) {
            DWORD* varray = *(DWORD**)(g_globals_container + 0x12C);
            if (varray && varray[index]) {
                BLZSStrCopy((char*)(varray[index] + 0x4C), value, 260);
            }
        }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

int __cdecl ydt_delete_trigger(int trig_index) {
    using namespace agent_api;
    if (trig_index < 0 || trig_index >= (int)g_triggers.size()) return 0;

    DWORD trig = g_triggers[trig_index];
    DWORD root = find_trigger_tree_root();
    if (!root) root = find_root_from_triggers();
    int result = delete_trigger_impl(trig, root);
    if (result) g_triggers.erase(g_triggers.begin() + trig_index);
    return result;
}

} // extern "C"
