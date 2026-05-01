#include <windows.h>
#include <vector>
#include <string>
#include <BlizzardStorm.h>

// ObjectAPI: Binary parser for Warcraft III object editor files
// (war3map.w3u, w3a, w3t, w3b, w3q — unit/ability/item/buff/upgrade data)

namespace object_api {

// Binary format: integer/real fields are 4 bytes, strings null-terminated padded to 4
// File header: 4-byte magic, 4-byte version, then original objects, then custom objects

struct ObjField {
	DWORD field_id; // 4-char code like 'unam', 'uabi'
	DWORD type; // 0=int, 1=real, 2=unreal, 3=string
	DWORD int_val; // for type 0
	float real_val; // for type 1/2
	char* str_val; // for type 3 (on heap)
};

struct ObjRecord {
	DWORD obj_id; // 4-char id like 'hpea'
	DWORD base_id; // 0 for original, base for custom
	std::vector<ObjField> fields;
};

struct ObjFile {
	DWORD magic;
	DWORD version;
	std::vector<ObjRecord> original;
	std::vector<ObjRecord> custom;
};

// Ring buffer for JSON output (shared with AgentAPI)
static char g_out_buf[65536];
static int g_out_pos = 0;

static void buf_write(const char* s, int len) {
	if (g_out_pos + len + 1 > (int)sizeof(g_out_buf)) g_out_pos = 0;
	memcpy(g_out_buf + g_out_pos, s, len);
	g_out_pos += len;
	g_out_buf[g_out_pos] = '\0';
}

static const char* buf_str() { return g_out_buf; }

static void buf_clear() { g_out_pos = 0; g_out_buf[0] = '\0'; }

// -- binary reader helpers --

static DWORD read_u32(const unsigned char*& p) {
	DWORD v = *(DWORD*)p;
	p += 4;
	return v;
}

static float read_f32(const unsigned char*& p) {
	float v = *(float*)p;
	p += 4;
	return v;
}

static DWORD make_id(const char* s) {
	return (*(DWORD*)s);
}

static void id_to_str(DWORD id, char* out) {
	out[0] = (char)(id & 0xFF);
	out[1] = (char)((id >> 8) & 0xFF);
	out[2] = (char)((id >> 16) & 0xFF);
	out[3] = (char)((id >> 24) & 0xFF);
	// Trim trailing null bytes (e.g. "W3U\0" -> "W3U")
	int len = 4;
	while (len > 0 && out[len - 1] == '\0') len--;
	out[len] = '\0';
}

// Parse binary object data into ObjFile
static bool parse_obj_data(const unsigned char* data, DWORD size, ObjFile& file) {
	if (size < 8) return false;
	const unsigned char* p = data;
	const unsigned char* end = data + size;

	file.magic = read_u32(p);
	file.version = read_u32(p);

	// Original objects
	if (p + 4 > end) return false;
	DWORD orig_count = read_u32(p);
	for (DWORD i = 0; i < orig_count; i++) {
		if (p + 8 > end) return false;
		ObjRecord rec;
		rec.obj_id = read_u32(p);
		rec.base_id = 0;
		DWORD mod_count = read_u32(p);

		for (DWORD j = 0; j < mod_count; j++) {
			if (p + 8 > end) return false;
			ObjField f;
			f.field_id = read_u32(p);
			f.type = read_u32(p);
			f.int_val = 0;
			f.real_val = 0.0f;
			f.str_val = nullptr;

			switch (f.type) {
			case 0: // int
				if (p + 4 > end) return false;
				f.int_val = (int)read_u32(p);
				break;
			case 1: // real
				if (p + 4 > end) return false;
				f.real_val = read_f32(p);
				break;
			case 2: // unreal (real + 4 bytes padding)
				if (p + 8 > end) return false;
				f.real_val = read_f32(p);
				p += 4; // skip padding
				break;
			case 3: { // string (null-terminated, padded to 4)
				const char* str_start = (const char*)p;
				int str_len = 0;
				while (p < end && *(const char*)p) { p++; str_len++; }
				if (p >= end) return false;
				p++; // skip null
				int padded = (str_len + 1 + 3) & ~3; // 4-byte align
				p += padded - (str_len + 1);
				f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, str_len + 1);
				if (f.str_val) { memcpy(f.str_val, str_start, str_len); f.str_val[str_len] = '\0'; }
				break;
			}
			default:
				return false;
			}
			rec.fields.push_back(f);
		}
		file.original.push_back(rec);
	}

	// Custom objects
	if (p + 4 > end) return false;
	DWORD cust_count = read_u32(p);
	for (DWORD i = 0; i < cust_count; i++) {
		if (p + 12 > end) return false;
		ObjRecord rec;
		rec.obj_id = read_u32(p);
		rec.base_id = read_u32(p);
		DWORD mod_count = read_u32(p);

		for (DWORD j = 0; j < mod_count; j++) {
			if (p + 8 > end) return false;
			ObjField f;
			f.field_id = read_u32(p);
			f.type = read_u32(p);
			f.int_val = 0;
			f.real_val = 0.0f;
			f.str_val = nullptr;

			switch (f.type) {
			case 0:
				if (p + 4 > end) return false;
				f.int_val = (int)read_u32(p);
				break;
			case 1:
				if (p + 4 > end) return false;
				f.real_val = read_f32(p);
				break;
			case 2:
				if (p + 8 > end) return false;
				f.real_val = read_f32(p);
				p += 4;
				break;
			case 3: {
				const char* str_start = (const char*)p;
				int str_len = 0;
				while (p < end && *(const char*)p) { p++; str_len++; }
				if (p >= end) return false;
				p++;
				int padded = (str_len + 1 + 3) & ~3;
				p += padded - (str_len + 1);
				f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, str_len + 1);
				if (f.str_val) { memcpy(f.str_val, str_start, str_len); f.str_val[str_len] = '\0'; }
				break;
			}
			default:
				return false;
			}
			rec.fields.push_back(f);
		}
		file.custom.push_back(rec);
	}

	return true;
}

// Free all allocated strings
static void free_obj_file(ObjFile& file) {
	for (auto& rec : file.original)
		for (auto& f : rec.fields)
			if (f.str_val) HeapFree(GetProcessHeap(), 0, f.str_val);
	for (auto& rec : file.custom)
		for (auto& f : rec.fields)
			if (f.str_val) HeapFree(GetProcessHeap(), 0, f.str_val);
}

// JSON field escape
static void json_append_str(const char* s) {
	if (!s) { buf_write("null", 4); return; }
	buf_write("\"", 1);
	while (*s) {
		char c = *s++;
		switch (c) {
			case '"':  buf_write("\\\"", 2); break;
			case '\\': buf_write("\\\\", 2); break;
			case '\n': buf_write("\\n", 2); break;
			case '\r': buf_write("\\r", 2); break;
			case '\t': buf_write("\\t", 2); break;
			default:
				if ((unsigned char)c < 0x20) {
					char hex[8]; BLZSStrPrintf(hex, 8, "\\u%04x", (unsigned char)c);
					buf_write(hex, 6);
				} else {
					buf_write(&c, 1);
				}
		}
	}
	buf_write("\"", 1);
}

// Convert ObjFile to JSON string
static const char* objfile_to_json(ObjFile& file) {
	buf_clear();

	buf_write("{", 1);

	// magic
	char id[5];
	id_to_str(file.magic, id);
	buf_write("\"magic\":\"", 9); buf_write(id, (int)BLZSStrLen(id)); buf_write("\"", 1);
	buf_write(",\"version\":", 11);
	char num[32];
	int nlen = BLZSStrPrintf(num, 32, "%u", file.version);
	buf_write(num, nlen);

	// original
	buf_write(",\"original\":[", 13);
	for (size_t i = 0; i < file.original.size(); i++) {
		if (i > 0) buf_write(",", 1);
		buf_write("{", 1);

		id_to_str(file.original[i].obj_id, id);
		buf_write("\"id\":\"", 6); buf_write(id, (int)BLZSStrLen(id)); buf_write("\",\"fields\":{", 12);

		auto& fields = file.original[i].fields;
		for (size_t j = 0; j < fields.size(); j++) {
			if (j > 0) buf_write(",", 1);
			auto& f = fields[j];
			id_to_str(f.field_id, id);
			buf_write("\"", 1); buf_write(id, (int)BLZSStrLen(id)); buf_write("\":", 2);

			switch (f.type) {
			case 0:
				nlen = BLZSStrPrintf(num, 32, "%d", f.int_val);
				buf_write(num, nlen);
				break;
			case 1:
			case 2: {
				char rbuf[64];
				nlen = BLZSStrPrintf(rbuf, 64, "%.6g", f.real_val);
				buf_write(rbuf, nlen);
				break;
			}
			case 3:
				json_append_str(f.str_val ? f.str_val : "");
				break;
			}
		}
		buf_write("}}", 2); // close fields and object
	}
	buf_write("]", 1);

	// custom
	buf_write(",\"custom\":[", 11);
	for (size_t i = 0; i < file.custom.size(); i++) {
		if (i > 0) buf_write(",", 1);
		buf_write("{", 1);

		auto& rec = file.custom[i];
		id_to_str(rec.obj_id, id);
		buf_write("\"id\":\"", 6); buf_write(id, (int)BLZSStrLen(id)); buf_write("\"", 1);
		id_to_str(rec.base_id, id);
		buf_write(",\"base\":\"", 9); buf_write(id, (int)BLZSStrLen(id)); buf_write("\",\"fields\":{", 12);

		auto& fields = rec.fields;
		for (size_t j = 0; j < fields.size(); j++) {
			if (j > 0) buf_write(",", 1);
			auto& f = fields[j];
			id_to_str(f.field_id, id);
			buf_write("\"", 1); buf_write(id, (int)BLZSStrLen(id)); buf_write("\":", 2);

			switch (f.type) {
			case 0:
				nlen = BLZSStrPrintf(num, 32, "%d", f.int_val);
				buf_write(num, nlen);
				break;
			case 1:
			case 2: {
				char rbuf[64];
				nlen = BLZSStrPrintf(rbuf, 64, "%.6g", f.real_val);
				buf_write(rbuf, nlen);
				break;
			}
			case 3:
				json_append_str(f.str_val ? f.str_val : "");
				break;
			}
		}
		buf_write("}}", 2);
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
	struct { bool b; double n; char* s; } data;
	std::vector<JsonVal*> arr;
	std::vector<std::pair<char*, JsonVal*>> obj;

	~JsonVal() {
		if (kind == STR && data.s) HeapFree(GetProcessHeap(), 0, data.s);
		for (auto& kv : obj) { HeapFree(GetProcessHeap(), 0, kv.first); delete kv.second; }
		for (auto& v : arr) delete v;
	}
};

static void skip_ws(const char*& p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

static JsonVal* json_parse_val(const char*& p) {
	skip_ws(p);
	if (!*p) return nullptr;

	JsonVal* v = new JsonVal();
	if (*p == '{') {
		v->kind = JsonVal::OBJECT;
		p++;
		skip_ws(p);
		if (*p != '}') {
			while (true) {
				skip_ws(p);
				if (*p != '"') { delete v; return nullptr; }
				p++;
				const char* ks = p;
				int kl = 0;
				while (*p && *p != '"') { if (*p == '\\') p++; p++; kl++; }
				if (!*p) { delete v; return nullptr; }
				char* key = (char*)HeapAlloc(GetProcessHeap(), 0, kl + 1);
				for (int i = 0; i < kl; i++) key[i] = ks[i];
				key[kl] = '\0';
				p++; // closing "
				skip_ws(p);
				if (*p != ':') { HeapFree(GetProcessHeap(), 0, key); delete v; return nullptr; }
				p++; // ':'
				JsonVal* child = json_parse_val(p);
				if (!child) { HeapFree(GetProcessHeap(), 0, key); delete v; return nullptr; }
				v->obj.push_back({key, child});
				skip_ws(p);
				if (*p == ',') { p++; continue; }
				if (*p == '}') { p++; break; }
				HeapFree(GetProcessHeap(), 0, key); delete v; return nullptr;
			}
		} else p++;
	} else if (*p == '[') {
		v->kind = JsonVal::ARRAY;
		p++;
		skip_ws(p);
		if (*p != ']') {
			while (true) {
				JsonVal* child = json_parse_val(p);
				if (!child) { delete v; return nullptr; }
				v->arr.push_back(child);
				skip_ws(p);
				if (*p == ',') { p++; continue; }
				if (*p == ']') { p++; break; }
				delete v; return nullptr;
			}
		} else p++;
	} else if (*p == '"') {
		v->kind = JsonVal::STR;
		p++;
		int sl = 0;
		const char* ss = p;
		while (*p && *p != '"') { if (*p == '\\') p++; p++; sl++; }
		v->data.s = (char*)HeapAlloc(GetProcessHeap(), 0, sl + 1);
		for (int i = 0; i < sl; i++) v->data.s[i] = ss[i];
		v->data.s[sl] = '\0';
		if (*p) p++; // closing "
	} else if (*p == 't' && p[1]=='r' && p[2]=='u' && p[3]=='e') {
		v->kind = JsonVal::BOOL; v->data.b = true; p += 4;
	} else if (*p == 'f' && p[1]=='a' && p[2]=='l' && p[3]=='s' && p[4]=='e') {
		v->kind = JsonVal::BOOL; v->data.b = false; p += 5;
	} else if (*p == 'n' && p[1]=='u' && p[2]=='l' && p[3]=='l') {
		v->kind = JsonVal::NUL; p += 4;
	} else if (*p == '-' || (*p >= '0' && *p <= '9')) {
		v->kind = JsonVal::NUM;
		v->data.n = strtod(p, (char**)&p);
	} else {
		delete v; return nullptr;
	}
	return v;
}

// Find a value in a JSON object by key
static JsonVal* obj_get(JsonVal* obj, const char* key) {
	if (obj->kind != JsonVal::OBJECT) return nullptr;
	for (auto& kv : obj->obj)
		if (!BLZSStrCmp(kv.first, key, 0x7FFFFFFF)) return kv.second;
	return nullptr;
}

// Encode ObjFile back to binary. Returns NULL on failure.
static std::vector<unsigned char> objfile_to_binary(ObjFile& file) {
	std::vector<unsigned char> out;

	auto w32 = [&](DWORD v) {
		out.push_back((unsigned char)(v & 0xFF));
		out.push_back((unsigned char)((v>>8) & 0xFF));
		out.push_back((unsigned char)((v>>16) & 0xFF));
		out.push_back((unsigned char)((v>>24) & 0xFF));
	};

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
			case 0: w32((DWORD)f.int_val); break;
			case 1: { DWORD v = *(DWORD*)&f.real_val; w32(v); break; }
			case 2: { DWORD v = *(DWORD*)&f.real_val; w32(v); w32(0); break; }
			case 3: {
				const char* s = f.str_val ? f.str_val : "";
				int sl = (int)BLZSStrLen(s);
				for (int i = 0; i < sl; i++) out.push_back(s[i]);
				out.push_back(0); // null
				int padded = (sl + 1 + 3) & ~3;
				for (int i = sl + 1; i < padded; i++) out.push_back(0);
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
			case 0: w32((DWORD)f.int_val); break;
			case 1: { DWORD v = *(DWORD*)&f.real_val; w32(v); break; }
			case 2: { DWORD v = *(DWORD*)&f.real_val; w32(v); w32(0); break; }
			case 3: {
				const char* s = f.str_val ? f.str_val : "";
				int sl = (int)BLZSStrLen(s);
				for (int i = 0; i < sl; i++) out.push_back(s[i]);
				out.push_back(0);
				int padded = (sl + 1 + 3) & ~3;
				for (int i = sl + 1; i < padded; i++) out.push_back(0);
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
	if (!root || root->kind != JsonVal::OBJECT) { delete root; return false; }

	// Read magic/version
	JsonVal* magic_v = obj_get(root, "magic");
	JsonVal* version_v = obj_get(root, "version");
	if (!magic_v || magic_v->kind != JsonVal::STR || !version_v || version_v->kind != JsonVal::NUM) {
		delete root; return false;
	}
	file.magic = make_id(magic_v->data.s);
	file.version = (DWORD)version_v->data.n;

	// Read original objects
	JsonVal* orig = obj_get(root, "original");
	if (orig && orig->kind == JsonVal::ARRAY) {
		for (auto* obj_v : orig->arr) {
			if (obj_v->kind != JsonVal::OBJECT) continue;
			ObjRecord rec;
			JsonVal* id_v = obj_get(obj_v, "id");
			if (id_v && id_v->kind == JsonVal::STR) rec.obj_id = make_id(id_v->data.s);
			rec.base_id = 0;

			JsonVal* fields_v = obj_get(obj_v, "fields");
			if (fields_v && fields_v->kind == JsonVal::OBJECT) {
				for (auto& kv : fields_v->obj) {
					ObjField f;
					f.field_id = make_id(kv.first);
					f.str_val = nullptr;
					f.int_val = 0;
					f.real_val = 0.0f;

					auto* fv = kv.second;
					switch (fv->kind) {
					case JsonVal::NUM:
						if (fv->data.n == (int)fv->data.n) {
							f.type = 0; f.int_val = (int)fv->data.n;
						} else {
							f.type = 1; f.real_val = (float)fv->data.n;
						}
						break;
					case JsonVal::STR:
						f.type = 3;
						f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, BLZSStrLen(fv->data.s) + 1);
						if (f.str_val) BLZSStrCopy(f.str_val, fv->data.s, BLZSStrLen(fv->data.s) + 1);
						break;
					case JsonVal::BOOL:
						f.type = 0; f.int_val = fv->data.b ? 1 : 0;
						break;
					case JsonVal::NUL:
						f.type = 3;
						f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, 1);
						if (f.str_val) f.str_val[0] = '\0';
						break;
					default:
						continue; // skip arrays/objects
					}
					rec.fields.push_back(f);
				}
			}
			file.original.push_back(rec);
		}
	}

	// Read custom objects
	JsonVal* cust = obj_get(root, "custom");
	if (cust && cust->kind == JsonVal::ARRAY) {
		for (auto* obj_v : cust->arr) {
			if (obj_v->kind != JsonVal::OBJECT) continue;
			ObjRecord rec;
			JsonVal* id_v = obj_get(obj_v, "id");
			JsonVal* base_v = obj_get(obj_v, "base");
			if (id_v && id_v->kind == JsonVal::STR) rec.obj_id = make_id(id_v->data.s);
			if (base_v && base_v->kind == JsonVal::STR) rec.base_id = make_id(base_v->data.s);

			JsonVal* fields_v = obj_get(obj_v, "fields");
			if (fields_v && fields_v->kind == JsonVal::OBJECT) {
				for (auto& kv : fields_v->obj) {
					ObjField f;
					f.field_id = make_id(kv.first);
					f.str_val = nullptr;
					f.int_val = 0;
					f.real_val = 0.0f;

					auto* fv = kv.second;
					switch (fv->kind) {
					case JsonVal::NUM:
						if (fv->data.n == (int)fv->data.n) {
							f.type = 0; f.int_val = (int)fv->data.n;
						} else {
							f.type = 1; f.real_val = (float)fv->data.n;
						}
						break;
					case JsonVal::STR:
						f.type = 3;
						f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, BLZSStrLen(fv->data.s) + 1);
						if (f.str_val) BLZSStrCopy(f.str_val, fv->data.s, BLZSStrLen(fv->data.s) + 1);
						break;
					case JsonVal::BOOL:
						f.type = 0; f.int_val = fv->data.b ? 1 : 0;
						break;
					case JsonVal::NUL:
						f.type = 3;
						f.str_val = (char*)HeapAlloc(GetProcessHeap(), 0, 1);
						if (f.str_val) f.str_val[0] = '\0';
						break;
					default:
						continue;
					}
					rec.fields.push_back(f);
				}
			}
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
	if (!file_path) return nullptr;

	// Read entire file
	HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return nullptr;

	DWORD size = GetFileSize(hFile, NULL);
	if (size < 8 || size > 50 * 1024 * 1024) { CloseHandle(hFile); return nullptr; }

	unsigned char* data = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, size);
	if (!data) { CloseHandle(hFile); return nullptr; }

	DWORD read;
	if (!ReadFile(hFile, data, size, &read, NULL) || read != size) {
		HeapFree(GetProcessHeap(), 0, data);
		CloseHandle(hFile);
		return nullptr;
	}
	CloseHandle(hFile);

	object_api::ObjFile file;
	if (!object_api::parse_obj_data(data, size, file)) {
		HeapFree(GetProcessHeap(), 0, data);
		return nullptr;
	}
	HeapFree(GetProcessHeap(), 0, data);

	const char* json = object_api::objfile_to_json(file);
	object_api::free_obj_file(file);
	return json;
}

int __cdecl ydt_write_object_file(const char* file_path, const char* json_data) {
	if (!file_path || !json_data) return 0;

	object_api::ObjFile file;
	if (!object_api::json_to_objfile(json_data, file)) return 0;

	auto binary = object_api::objfile_to_binary(file);
	object_api::free_obj_file(file);
	if (binary.empty()) return 0;

	HANDLE hFile = CreateFileA(file_path, GENERIC_WRITE, 0,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE) return 0;

	DWORD written;
	BOOL ok = WriteFile(hFile, binary.data(), (DWORD)binary.size(), &written, NULL);
	CloseHandle(hFile);
	return (ok && written == binary.size()) ? 1 : 0;
}

} // extern "C"
