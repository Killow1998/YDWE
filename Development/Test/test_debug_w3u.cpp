#include "catch2/catch_amalgamated.hpp"
#include <windows.h>
#include <vector>
#include <cstdio>

extern "C" {
const char* __cdecl ydt_read_object_file(const char* file_path);
}

TEST_CASE("Debug - read real w3u from UI test map", "[debug_w3u]") {
    const char* path = "Q:/AppData/ydwe/YDWE/Development/Component/example(演示地图)/综合——UI测试地图1.19.w3xTemp/war3map.w3u";
    
    // Check if file exists
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("File not found: %s\n", path);
        return;
    }
    DWORD size = GetFileSize(h, NULL);
    printf("File size: %u bytes\n", size);
    
    // Print first 32 bytes as hex
    unsigned char buf[32];
    DWORD read;
    ReadFile(h, buf, 32, &read, NULL);
    CloseHandle(h);
    
    printf("First 32 bytes:");
    for (DWORD i = 0; i < read && i < 32; i++) printf(" %02x", buf[i]);
    printf("\n");
    
    // Try reading via API
    printf("Calling ydt_read_object_file...\n");
    const char* json = ydt_read_object_file(path);
    if (json) {
        printf("OK! JSON length: %d\n", (int)strlen(json));
        printf("First 200 chars: %.200s\n", json);
    } else {
        printf("Returned NULL (error or parse failed)\n");
    }
}
