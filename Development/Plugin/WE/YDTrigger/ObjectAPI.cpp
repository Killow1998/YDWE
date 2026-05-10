#include <windows.h>
#include <vector>
#include <string>
#include <cstring>
#include <BlizzardStorm.h>

// ObjectAPI: Binary parser for Warcraft III object editor files
// (war3map.w3u, w3a, w3t, w3b, w3q — unit/ability/item/buff/upgrade data)

namespace object_api {

// Binary format: integer/real fields are 4 bytes, strings null-terminated padded to 4
// File header: 4-byte magic, 4-byte version, then original objects, then custom objects

struct ObjField {
    DWORD field_id; // 4-char code like 'unam', 'uabi'
    DWORD type;     // 0=int, 1=real, 2=unreal, 3=string
    DWORD level;    // ability/doodad/upgrade object files store level/data before value
    DWORD data_id;
    DWORD terminator;
    DWORD int_val;  // for type 0
    float real_val; // for type 1/2
    char* str_val;  // for type 3 (on heap)
};

struct ObjRecord {
    DWORD obj_id;  // 4-char id like 'hpea'
    DWORD base_id; // 0 for original, base for custom
    std::vector<ObjField> fields;
};

struct ObjFile {
    DWORD magic;
    DWORD version;
    bool real_layout;
    bool has_level;
    std::vector<ObjRecord> original;
    std::vector<ObjRecord> custom;
};

static std::string g_out_buf;

static void buf_write(const char* s, int len) {
    if (s && len > 0)
        g_out_buf.append(s, len);
}

static const char* buf_str() {
    return g_out_buf.c_str();
}

static void buf_clear() {
    g_out_buf.clear();
}

// -- binary reader helpers --

static bool can_read(const unsigned char* p, const unsigned char* end, size_t bytes) {
    return p <= end && (size_t)(end - p) >= bytes;
}

static bool read_u32_checked(const unsigned char*& p, const unsigned char* end, DWORD& out) {
    if (!can_read(p, end, 4))
        return false;
    out = (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24);
    p += 4;
    return true;
}

static bool read_f32_checked(const unsigned char*& p, const unsigned char* end, float& out) {
    if (!can_read(p, end, 4))
        return false;
    DWORD bits;
    if (!read_u32_checked(p, end, bits))
        return false;
    memcpy(&out, &bits, sizeof(float));
    return true;
}

static DWORD make_id(const char* s) {
    DWORD v = 0;
    if (!s)
        return 0;
    for (int i = 0; i < 4 && s[i]; ++i)
        v |= ((DWORD)(unsigned char)s[i]) << (i * 8);
    return v;
}

static void id_to_str(DWORD id, char* out) {
    out[0] = (char)(id & 0xFF);
    out[1] = (char)((id >> 8) & 0xFF);
    out[2] = (char)((id >> 16) & 0xFF);
    out[3] = (char)((id >> 24) & 0xFF);
    // Trim trailing null bytes (e.g. "W3U\0" -> "W3U")
    int len = 4;
    while (len > 0 && out[len - 1] == '\0')
        len--;
    out[len] = '\0';
}

static bool looks_like_legacy_magic(DWORD value) {
    char id[5];
    id_to_str(value, id);
    return id[0] == 'W' && id[1] == '3';
}

static bool has_level_for_path(const char* file_path) {
    if (!file_path)
        return false;
    const char* dot = strrchr(file_path, '.');
    if (!dot)
        return false;
    return !_stricmp(dot, ".w3a") || !_stricmp(dot, ".w3d") || !_stricmp(dot, ".w3q");
}

static char* heap_copy(const char* data, size_t len) {
    char* out = (char*)HeapAlloc(GetProcessHeap(), 0, len + 1);
    if (!out)
        return nullptr;
    if (len > 0)
        memcpy(out, data, len);
    out[len] = '\0';
    return out;
}

static bool read_zero_string(const unsigned char*& p, const unsigned char* end, char*& out, bool align_four) {
    const unsigned char* start = p;
    while (p < end && *p != 0)
        ++p;
    if (p >= end)
        return false;
    size_t len = (size_t)(p - start);
    size_t skip = align_four ? ((((len + 1u) + 3u) & ~3u) - len) : 1u;
    if (!can_read(p, end, skip))
        return false;
    out = heap_copy((const char*)start, len);
    if (!out)
        return false;
    p += skip;
    return out != nullptr;
}

static bool read_field(const unsigned char*& p, const unsigned char* end, bool is_real_layout, bool has_level, ObjField& f) {
    if (!read_u32_checked(p, end, f.field_id) || !read_u32_checked(p, end, f.type))
        return false;
    f.level = 0;
    f.data_id = 0;
    f.terminator = 0;
    f.int_val = 0;
    f.real_val = 0.0f;
    f.str_val = nullptr;

    if (has_level) {
        if (!read_u32_checked(p, end, f.level) || !read_u32_checked(p, end, f.data_id))
            return false;
    }

    switch (f.type) {
    case 0:
        if (!read_u32_checked(p, end, f.int_val))
            return false;
        if (is_real_layout)
            if (!read_u32_checked(p, end, f.terminator))
                return false;
        return true;
    case 1:
        if (!read_f32_checked(p, end, f.real_val))
            return false;
        if (is_real_layout)
            return read_u32_checked(p, end, f.terminator);
        return true;
    case 2:
        if (!read_f32_checked(p, end, f.real_val))
            return false;
        if (is_real_layout)
            return read_u32_checked(p, end, f.terminator);
        return read_u32_checked(p, end, f.terminator); // legacy unreal includes padding
    case 3:
        if (!read_zero_string(p, end, f.str_val, !is_real_layout))
            return false;
        if (!is_real_layout)
            return true;
        if (!read_u32_checked(p, end, f.terminator))
            return false;
        return true;
    default:
        return false;
    }
}

static bool read_chunk(
    const unsigned char*& p,
    const unsigned char* end,
    bool is_real_layout,
    bool custom,
    bool has_level,
    std::vector<ObjRecord>& out
) {
    DWORD count = 0;
    if (!read_u32_checked(p, end, count) || count > 100000)
        return false;
    for (DWORD i = 0; i < count; ++i) {
        DWORD obj_id = 0;
        DWORD base_id = 0;
        DWORD mod_count = 0;
        if (is_real_layout) {
            DWORD parent = 0;
            DWORD name = 0;
            if (!read_u32_checked(p, end, parent) || !read_u32_checked(p, end, name) || !read_u32_checked(p, end, mod_count))
                return false;
            obj_id = name == 0 ? parent : name;
            base_id = custom ? parent : 0;
        } else {
            if (!read_u32_checked(p, end, obj_id))
                return false;
            if (custom && !read_u32_checked(p, end, base_id))
                return false;
            if (!read_u32_checked(p, end, mod_count))
                return false;
            if (!custom)
                base_id = 0;
        }
        if (mod_count > 100000)
            return false;

        ObjRecord rec;
        rec.obj_id = obj_id;
        rec.base_id = base_id;
        for (DWORD j = 0; j < mod_count; ++j) {
            ObjField f;
            if (!read_field(p, end, is_real_layout, has_level, f))
                return false;
            rec.fields.push_back(f);
        }
        out.push_back(rec);
    }
    return true;
}

static bool parse_real_obj_data(const unsigned char* data, DWORD size, bool has_level, ObjFile& file) {
    const unsigned char* p = data;
    const unsigned char* end = data + size;
    DWORD version = 0;
    if (!read_u32_checked(p, end, version))
        return false;
    file.magic = 0;
    file.version = version;
    file.real_layout = true;
    file.has_level = has_level;
    if (!read_chunk(p, end, true, false, has_level, file.original))
        return false;
    if (!read_chunk(p, end, true, true, has_level, file.custom))
        return false;
    return p <= end;
}

// Parse binary object data into ObjFile
static bool parse_obj_data(const unsigned char* data, DWORD size, ObjFile& file) {
    if (size < 8)
        return false;
    const unsigned char* p = data;
    const unsigned char* end = data + size;

    if (!read_u32_checked(p, end, file.magic) || !read_u32_checked(p, end, file.version))
        return false;
    file.real_layout = false;
    file.has_level = false;
    if (!read_chunk(p, end, false, false, false, file.original))
        return false;
    if (!read_chunk(p, end, false, true, false, file.custom))
        return false;
    return true;
}

// Free all allocated strings
static void free_obj_file(ObjFile& file) {
    for (auto& rec : file.original)
        for (auto& f : rec.fields)
            if (f.str_val)
                HeapFree(GetProcessHeap(), 0, f.str_val);
    for (auto& rec : file.custom)
        for (auto& f : rec.fields)
            if (f.str_val)
                HeapFree(GetProcessHeap(), 0, f.str_val);
}

// JSON field escape
static void json_append_str(const char* s) {
    if (!s) {
        buf_write("null", 4);
        return;
    }
    buf_write("\"", 1);
    while (*s) {
        char c = *s++;
        switch (c) {
        case '"':
            buf_write("\\\"", 2);
            break;
        case '\\':
            buf_write("\\\\", 2);
            break;
        case '\n':
            buf_write("\\n", 2);
            break;
        case '\r':
            buf_write("\\r", 2);
            break;
        case '\t':
            buf_write("\\t", 2);
            break;
        default:
            if ((unsigned char)c < 0x20) {
                char hex[8];
                BLZSStrPrintf(hex, 8, "\\u%04x", (unsigned char)c);
                buf_write(hex, 6);
            } else {
                buf_write(&c, 1);
            }
        }
    }
    buf_write("\"", 1);
}

static void json_append_field_scalar(ObjField& f) {
    char num[64];
    int nlen = 0;
    switch (f.type) {
    case 0:
        nlen = BLZSStrPrintf(num, 64, "%d", f.int_val);
        buf_write(num, nlen);
        break;
    case 1:
    case 2:
        nlen = BLZSStrPrintf(num, 64, "%.6g", f.real_val);
        buf_write(num, nlen);
        break;
    case 3:
        json_append_str(f.str_val ? f.str_val : "");
        break;
    default:
        buf_write("null", 4);
        break;
    }
}

static void json_append_field_details(std::vector<ObjField>& fields) {
    char id[5];
    char num[64];

    buf_write(",\"field_details\":[", 18);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0)
            buf_write(",", 1);
        auto& f = fields[i];
        id_to_str(f.field_id, id);
        buf_write("{\"id\":\"", 7);
        buf_write(id, (int)BLZSStrLen(id));
        buf_write("\",\"type\":", 9);
        int nlen = BLZSStrPrintf(num, 64, "%u", f.type);
        buf_write(num, nlen);
        buf_write(",\"level\":", 9);
        nlen = BLZSStrPrintf(num, 64, "%u", f.level);
        buf_write(num, nlen);
        buf_write(",\"data\":", 8);
        nlen = BLZSStrPrintf(num, 64, "%u", f.data_id);
        buf_write(num, nlen);
        buf_write(",\"terminator\":", 14);
        nlen = BLZSStrPrintf(num, 64, "%u", f.terminator);
        buf_write(num, nlen);
        buf_write(",\"value\":", 9);
        json_append_field_scalar(f);
        buf_write("}", 1);
    }
    buf_write("]", 1);
}

// Convert ObjFile to JSON string
static const char* objfile_to_json(ObjFile& file) {
    buf_clear();

    buf_write("{", 1);

    // magic
    char id[5];
    id_to_str(file.magic, id);
    buf_write("\"magic\":\"", 9);
    buf_write(id, (int)BLZSStrLen(id));
    buf_write("\"", 1);
    buf_write(",\"version\":", 11);
    char num[32];
    int nlen = BLZSStrPrintf(num, 32, "%u", file.version);
    buf_write(num, nlen);

    // original
    buf_write(",\"original\":[", 13);
    for (size_t i = 0; i < file.original.size(); i++) {
        if (i > 0)
            buf_write(",", 1);
        buf_write("{", 1);

        id_to_str(file.original[i].obj_id, id);
        buf_write("\"id\":\"", 6);
        buf_write(id, (int)BLZSStrLen(id));
        buf_write("\",\"fields\":{", 12);

        auto& fields = file.original[i].fields;
        for (size_t j = 0; j < fields.size(); j++) {
            if (j > 0)
                buf_write(",", 1);
            auto& f = fields[j];
            id_to_str(f.field_id, id);
            buf_write("\"", 1);
            buf_write(id, (int)BLZSStrLen(id));
            buf_write("\":", 2);

            json_append_field_scalar(f);
        }
        buf_write("}", 1); // close fields
        if (file.real_layout)
            json_append_field_details(fields);
        buf_write("}", 1);
    }
    buf_write("]", 1);

    // custom
    buf_write(",\"custom\":[", 11);
    for (size_t i = 0; i < file.custom.size(); i++) {
        if (i > 0)
            buf_write(",", 1);
        buf_write("{", 1);

        auto& rec = file.custom[i];
        id_to_str(rec.obj_id, id);
        buf_write("\"id\":\"", 6);
        buf_write(id, (int)BLZSStrLen(id));
        buf_write("\"", 1);
        id_to_str(rec.base_id, id);
        buf_write(",\"base\":\"", 9);
        buf_write(id, (int)BLZSStrLen(id));
        buf_write("\",\"fields\":{", 12);

        auto& fields = rec.fields;
        for (size_t j = 0; j < fields.size(); j++) {
            if (j > 0)
                buf_write(",", 1);
            auto& f = fields[j];
            id_to_str(f.field_id, id);
            buf_write("\"", 1);
            buf_write(id, (int)BLZSStrLen(id));
            buf_write("\":", 2);

            json_append_field_scalar(f);
        }
        buf_write("}", 1);
        if (file.real_layout)
            json_append_field_details(fields);
        buf_write("}", 1);
    }
    buf_write("]", 1);

    buf_write("}", 1);
    return buf_str();
}

// ====== JSON → Binary ======

// Simple JSON value parser
struct JsonVal;
static JsonVal* json_parse_val(const char*& p);
static void json_free(JsonVal* v);

struct JsonVal {
    enum { NUL, BOOL, NUM, STR, ARRAY, OBJECT } kind;
    struct {
        bool b;
        double n;
        char* s;
    } data;
    std::vector<JsonVal*> arr;
    std::vector<std::pair<char*, JsonVal*>> obj;

    ~JsonVal() {
        if (kind == STR && data.s)
            HeapFree(GetProcessHeap(), 0, data.s);
        for (auto& kv : obj) {
            HeapFree(GetProcessHeap(), 0, kv.first);
            delete kv.second;
        }
        for (auto& v : arr)
            delete v;
    }
};

static void skip_ws(const char*& p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
}

static JsonVal* json_parse_val(const char*& p) {
    skip_ws(p);
    if (!*p)
        return nullptr;

    JsonVal* v = new JsonVal();
    if (*p == '{') {
        v->kind = JsonVal::OBJECT;
        p++;
        skip_ws(p);
        if (*p != '}') {
            while (true) {
                skip_ws(p);
                if (*p != '"') {
                    delete v;
                    return nullptr;
                }
                p++;
                const char* ks = p;
                int kl = 0;
                while (*p && *p != '"') {
                    if (*p == '\\')
                        p++;
                    p++;
                    kl++;
                }
                if (!*p) {
                    delete v;
                    return nullptr;
                }
                char* key = (char*)HeapAlloc(GetProcessHeap(), 0, kl + 1);
                for (int i = 0; i < kl; i++)
                    key[i] = ks[i];
                key[kl] = '\0';
                p++; // closing "
                skip_ws(p);
                if (*p != ':') {
                    HeapFree(GetProcessHeap(), 0, key);
                    delete v;
                    return nullptr;
                }
                p++; // ':'
                JsonVal* child = json_parse_val(p);
                if (!child) {
                    HeapFree(GetProcessHeap(), 0, key);
                    delete v;
                    return nullptr;
                }
                v->obj.push_back({key, child});
                skip_ws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == '}') {
                    p++;
                    break;
                }
                HeapFree(GetProcessHeap(), 0, key);
                delete v;
                return nullptr;
            }
        } else
            p++;
    } else if (*p == '[') {
        v->kind = JsonVal::ARRAY;
        p++;
        skip_ws(p);
        if (*p != ']') {
            while (true) {
                JsonVal* child = json_parse_val(p);
                if (!child) {
                    delete v;
                    return nullptr;
                }
                v->arr.push_back(child);
                skip_ws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                if (*p == ']') {
                    p++;
                    break;
                }
                delete v;
                return nullptr;
            }
        } else
            p++;
    } else if (*p == '"') {
        v->kind = JsonVal::STR;
        p++;
        int sl = 0;
        const char* ss = p;
        while (*p && *p != '"') {
            if (*p == '\\')
                p++;
            p++;
            sl++;
        }
        v->data.s = (char*)HeapAlloc(GetProcessHeap(), 0, sl + 1);
        for (int i = 0; i < sl; i++)
            v->data.s[i] = ss[i];
        v->data.s[sl] = '\0';
        if (*p)
            p++; // closing "
    } else if (*p == 't' && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
        v->kind = JsonVal::BOOL;
        v->data.b = true;
        p += 4;
    } else if (*p == 'f' && p[1] == 'a' && p[2] == 'l' && p[3] == 's' && p[4] == 'e') {
        v->kind = JsonVal::BOOL;
        v->data.b = false;
        p += 5;
    } else if (*p == 'n' && p[1] == 'u' && p[2] == 'l' && p[3] == 'l') {
        v->kind = JsonVal::NUL;
        p += 4;
    } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
        v->kind = JsonVal::NUM;
        v->data.n = strtod(p, (char**)&p);
    } else {
        delete v;
        return nullptr;
    }
    return v;
}

// Find a value in a JSON object by key
static JsonVal* obj_get(JsonVal* obj, const char* key) {
    if (obj->kind != JsonVal::OBJECT)
        return nullptr;
    for (auto& kv : obj->obj)
        if (!BLZSStrCmp(kv.first, key, 0x7FFFFFFF))
            return kv.second;
    return nullptr;
}

static char* heap_dup_json_str(const char* s) {
    if (!s)
        s = "";
    size_t len = BLZSStrLen(s);
    char* out = (char*)HeapAlloc(GetProcessHeap(), 0, len + 1);
    if (out)
        BLZSStrCopy(out, s, len + 1);
    return out;
}

static bool fill_field_value(ObjField& f, JsonVal* fv, DWORD forced_type, bool has_forced_type) {
    if (!fv)
        return false;

    f.type = has_forced_type ? forced_type : 0;
    f.int_val = 0;
    f.real_val = 0.0f;
    f.str_val = nullptr;

    if (has_forced_type) {
        switch (forced_type) {
        case 0:
            if (fv->kind == JsonVal::NUM)
                f.int_val = (DWORD)fv->data.n;
            else if (fv->kind == JsonVal::BOOL)
                f.int_val = fv->data.b ? 1 : 0;
            else
                return false;
            return true;
        case 1:
        case 2:
            if (fv->kind != JsonVal::NUM)
                return false;
            f.real_val = (float)fv->data.n;
            return true;
        case 3:
            if (fv->kind == JsonVal::STR)
                f.str_val = heap_dup_json_str(fv->data.s);
            else if (fv->kind == JsonVal::NUL)
                f.str_val = heap_dup_json_str("");
            else
                return false;
            return f.str_val != nullptr;
        default:
            return false;
        }
    }

    switch (fv->kind) {
    case JsonVal::NUM:
        if (fv->data.n == (int)fv->data.n) {
            f.type = 0;
            f.int_val = (DWORD)fv->data.n;
        } else {
            f.type = 1;
            f.real_val = (float)fv->data.n;
        }
        return true;
    case JsonVal::STR:
        f.type = 3;
        f.str_val = heap_dup_json_str(fv->data.s);
        return f.str_val != nullptr;
    case JsonVal::BOOL:
        f.type = 0;
        f.int_val = fv->data.b ? 1 : 0;
        return true;
    case JsonVal::NUL:
        f.type = 3;
        f.str_val = heap_dup_json_str("");
        return f.str_val != nullptr;
    default:
        return false;
    }
}

static bool field_from_detail(JsonVal* detail_v, ObjField& f) {
    if (!detail_v || detail_v->kind != JsonVal::OBJECT)
        return false;

    JsonVal* id_v = obj_get(detail_v, "id");
    JsonVal* type_v = obj_get(detail_v, "type");
    JsonVal* value_v = obj_get(detail_v, "value");
    if (!id_v || id_v->kind != JsonVal::STR || !type_v || type_v->kind != JsonVal::NUM)
        return false;

    f.field_id = make_id(id_v->data.s);
    f.level = 0;
    f.data_id = 0;
    f.terminator = 0;

    JsonVal* level_v = obj_get(detail_v, "level");
    JsonVal* data_v = obj_get(detail_v, "data");
    JsonVal* terminator_v = obj_get(detail_v, "terminator");
    if (level_v && level_v->kind == JsonVal::NUM)
        f.level = (DWORD)level_v->data.n;
    if (data_v && data_v->kind == JsonVal::NUM)
        f.data_id = (DWORD)data_v->data.n;
    if (terminator_v && terminator_v->kind == JsonVal::NUM)
        f.terminator = (DWORD)terminator_v->data.n;

    return fill_field_value(f, value_v, (DWORD)type_v->data.n, true);
}

static void read_fields_from_json(JsonVal* obj_v, ObjRecord& rec) {
    JsonVal* details_v = obj_get(obj_v, "field_details");
    if (details_v && details_v->kind == JsonVal::ARRAY) {
        for (auto* detail : details_v->arr) {
            ObjField f;
            if (field_from_detail(detail, f))
                rec.fields.push_back(f);
        }
        return;
    }

    JsonVal* fields_v = obj_get(obj_v, "fields");
    if (!fields_v || fields_v->kind != JsonVal::OBJECT)
        return;
    for (auto& kv : fields_v->obj) {
        ObjField f;
        f.field_id = make_id(kv.first);
        f.level = 0;
        f.data_id = 0;
        f.terminator = 0;
        if (fill_field_value(f, kv.second, 0, false))
            rec.fields.push_back(f);
    }
}

// Encode ObjFile back to binary. Returns NULL on failure.
static std::vector<unsigned char> objfile_to_binary(ObjFile& file) {
    std::vector<unsigned char> out;

    auto w32 = [&](DWORD v) {
        out.push_back((unsigned char)(v & 0xFF));
        out.push_back((unsigned char)((v >> 8) & 0xFF));
        out.push_back((unsigned char)((v >> 16) & 0xFF));
        out.push_back((unsigned char)((v >> 24) & 0xFF));
    };

    if (file.real_layout) {
        auto wstr = [&](const char* s) {
            if (!s)
                s = "";
            for (const char* p = s; *p; ++p)
                out.push_back((unsigned char)*p);
            out.push_back(0);
        };
        auto write_field = [&](ObjField& f) {
            w32(f.field_id);
            w32(f.type);
            if (file.has_level) {
                w32(f.level);
                w32(f.data_id);
            }
            switch (f.type) {
            case 0:
                w32((DWORD)f.int_val);
                break;
            case 1:
            case 2: {
                DWORD v = *(DWORD*)&f.real_val;
                w32(v);
                break;
            }
            case 3:
                wstr(f.str_val);
                break;
            default:
                break;
            }
            w32(f.terminator);
        };
        auto write_chunk = [&](std::vector<ObjRecord>& chunk, bool custom) {
            w32((DWORD)chunk.size());
            for (auto& rec : chunk) {
                if (custom) {
                    w32(rec.base_id);
                    w32(rec.obj_id);
                } else {
                    w32(rec.obj_id);
                    w32(0);
                }
                w32((DWORD)rec.fields.size());
                for (auto& f : rec.fields)
                    write_field(f);
            }
        };

        w32(file.version);
        write_chunk(file.original, false);
        write_chunk(file.custom, true);
        return out;
    }

    w32(file.magic);
    w32(file.version);

    // Original
    w32((DWORD)file.original.size());
    for (auto& rec : file.original) {
        w32(rec.obj_id);
        w32((DWORD)rec.fields.size());
        for (auto& f : rec.fields) {
            w32(f.field_id);
            w32(f.type);
            switch (f.type) {
            case 0:
                w32((DWORD)f.int_val);
                break;
            case 1: {
                DWORD v = *(DWORD*)&f.real_val;
                w32(v);
                break;
            }
            case 2: {
                DWORD v = *(DWORD*)&f.real_val;
                w32(v);
                w32(0);
                break;
            }
            case 3: {
                const char* s = f.str_val ? f.str_val : "";
                int sl = (int)BLZSStrLen(s);
                for (int i = 0; i < sl; i++)
                    out.push_back(s[i]);
                out.push_back(0); // null
                int padded = (sl + 1 + 3) & ~3;
                for (int i = sl + 1; i < padded; i++)
                    out.push_back(0);
                break;
            }
            }
        }
    }

    // Custom
    w32((DWORD)file.custom.size());
    for (auto& rec : file.custom) {
        w32(rec.obj_id);
        w32(rec.base_id);
        w32((DWORD)rec.fields.size());
        for (auto& f : rec.fields) {
            w32(f.field_id);
            w32(f.type);
            switch (f.type) {
            case 0:
                w32((DWORD)f.int_val);
                break;
            case 1: {
                DWORD v = *(DWORD*)&f.real_val;
                w32(v);
                break;
            }
            case 2: {
                DWORD v = *(DWORD*)&f.real_val;
                w32(v);
                w32(0);
                break;
            }
            case 3: {
                const char* s = f.str_val ? f.str_val : "";
                int sl = (int)BLZSStrLen(s);
                for (int i = 0; i < sl; i++)
                    out.push_back(s[i]);
                out.push_back(0);
                int padded = (sl + 1 + 3) & ~3;
                for (int i = sl + 1; i < padded; i++)
                    out.push_back(0);
                break;
            }
            }
        }
    }

    return out;
}

// Parse JSON into ObjFile
static bool json_to_objfile(const char* json, ObjFile& file) {
    const char* p = json;
    JsonVal* root = json_parse_val(p);
    if (!root || root->kind != JsonVal::OBJECT) {
        delete root;
        return false;
    }

    // Read magic/version
    JsonVal* magic_v = obj_get(root, "magic");
    JsonVal* version_v = obj_get(root, "version");
    if (!magic_v || magic_v->kind != JsonVal::STR || !version_v || version_v->kind != JsonVal::NUM) {
        delete root;
        return false;
    }
    file.magic = make_id(magic_v->data.s);
    file.version = (DWORD)version_v->data.n;
    file.real_layout = false;
    file.has_level = false;

    // Read original objects
    JsonVal* orig = obj_get(root, "original");
    if (orig && orig->kind == JsonVal::ARRAY) {
        for (auto* obj_v : orig->arr) {
            if (obj_v->kind != JsonVal::OBJECT)
                continue;
            ObjRecord rec;
            rec.obj_id = 0;
            rec.base_id = 0;
            JsonVal* id_v = obj_get(obj_v, "id");
            if (id_v && id_v->kind == JsonVal::STR)
                rec.obj_id = make_id(id_v->data.s);
            rec.base_id = 0;

            read_fields_from_json(obj_v, rec);
            file.original.push_back(rec);
        }
    }

    // Read custom objects
    JsonVal* cust = obj_get(root, "custom");
    if (cust && cust->kind == JsonVal::ARRAY) {
        for (auto* obj_v : cust->arr) {
            if (obj_v->kind != JsonVal::OBJECT)
                continue;
            ObjRecord rec;
            rec.obj_id = 0;
            rec.base_id = 0;
            JsonVal* id_v = obj_get(obj_v, "id");
            JsonVal* base_v = obj_get(obj_v, "base");
            if (id_v && id_v->kind == JsonVal::STR)
                rec.obj_id = make_id(id_v->data.s);
            if (base_v && base_v->kind == JsonVal::STR)
                rec.base_id = make_id(base_v->data.s);

            read_fields_from_json(obj_v, rec);
            file.custom.push_back(rec);
        }
    }

    delete root;
    return true;
}

} // namespace object_api

// ========================================================================
// Exported C API
// ========================================================================

extern "C" {

const char* __cdecl ydt_read_object_file(const char* file_path) {
    if (!file_path)
        return nullptr;

    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return nullptr;

    DWORD size = GetFileSize(hFile, NULL);
    if (size < 8 || size > 50 * 1024 * 1024) {
        CloseHandle(hFile);
        return nullptr;
    }

    unsigned char* data = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, size);
    if (!data) {
        CloseHandle(hFile);
        return nullptr;
    }

    DWORD read;
    if (!ReadFile(hFile, data, size, &read, NULL) || read != size) {
        HeapFree(GetProcessHeap(), 0, data);
        CloseHandle(hFile);
        return nullptr;
    }
    CloseHandle(hFile);

    object_api::ObjFile file;
    DWORD first = size >= 4 ? *(DWORD*)data : 0;
    bool parsed = false;
    if (object_api::looks_like_legacy_magic(first)) {
        parsed = object_api::parse_obj_data(data, size, file);
    } else {
        parsed = object_api::parse_real_obj_data(data, size, object_api::has_level_for_path(file_path), file);
    }
    if (!parsed) {
        HeapFree(GetProcessHeap(), 0, data);
        return nullptr;
    }
    HeapFree(GetProcessHeap(), 0, data);

    const char* json = object_api::objfile_to_json(file);
    object_api::free_obj_file(file);
    return json;
}

static int safe_json_to_binary(const char* json_data, std::vector<unsigned char>& out) {
    object_api::ObjFile file;
    if (!object_api::json_to_objfile(json_data, file))
        return 0;
    out = object_api::objfile_to_binary(file);
    object_api::free_obj_file(file);
    return out.empty() ? 0 : 1;
}


int __cdecl ydt_write_object_file(const char* file_path, const char* json_data) {
    if (!file_path || !json_data)
        return 0;

    object_api::ObjFile file;
    if (!object_api::json_to_objfile(json_data, file))
        return 0;
    if (!object_api::looks_like_legacy_magic(file.magic)) {
        file.real_layout = true;
        file.has_level = object_api::has_level_for_path(file_path);
    }

    auto binary = object_api::objfile_to_binary(file);
    object_api::free_obj_file(file);
    if (binary.empty())
        return 0;

    HANDLE hFile = CreateFileA(file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;

    DWORD written;
    BOOL ok = WriteFile(hFile, binary.data(), (DWORD)binary.size(), &written, NULL);
    CloseHandle(hFile);
    return (ok && written == binary.size()) ? 1 : 0;
}

} // extern "C"
