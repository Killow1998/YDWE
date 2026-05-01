#pragma once

// Warcraft III object editor file constants
#define YDT_OBJ_UNIT    0   // war3map.w3u
#define YDT_OBJ_ITEM    1   // war3map.w3t
#define YDT_OBJ_BUFF    2   // war3map.w3b
#define YDT_OBJ_DOODAD  3   // war3map.w3d
#define YDT_OBJ_ABILITY 4   // war3map.w3a
#define YDT_OBJ_HERO    5   // war3map.w3h
#define YDT_OBJ_UPGRADE 6   // war3map.w3q

#ifdef __cplusplus
extern "C" {
#endif

// Read object data file (w3u/w3a/etc) and return JSON string.
// file_path: full path to the binary file (e.g. "...\war3map.w3u")
// Returns JSON string (valid until next API call), or NULL on error.
__declspec(dllexport) const char* __cdecl ydt_read_object_file(const char* file_path);

// Write JSON object data back to binary file.
// Returns 1 on success, 0 on failure.
__declspec(dllexport) int __cdecl ydt_write_object_file(const char* file_path, const char* json_data);

#ifdef __cplusplus
} // extern "C"
#endif
