// test_object_api.cpp — ObjectAPI binary format parser/writer tests
#include "catch2/catch_amalgamated.hpp"
#include <windows.h>
#include <BlizzardStorm.h>

// The read/write functions operate on files.
// We create temp files with known binary content and verify roundtrip.
extern "C" {
const char* __cdecl ydt_read_object_file(const char* file_path);
int __cdecl ydt_write_object_file(const char* file_path, const char* json_data);
}

// Build a minimal w3u binary blob in memory
static std::vector<unsigned char> make_minimal_w3u() {
    std::vector<unsigned char> out;
    auto w32 = [&](DWORD v) {
        for (int i = 0; i < 4; i++)
            out.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
    };
    // Magic "W3U\0"
    out.push_back('W');
    out.push_back('3');
    out.push_back('U');
    out.push_back(0);
    w32(1); // version
    w32(1); // 1 original object
    // Object: id="hfoo"
    out.push_back('h');
    out.push_back('f');
    out.push_back('o');
    out.push_back('o');
    w32(2); // 2 modifications
    // Mod 1: "unam" (name), type=3 (string), value="TestUnit"
    out.push_back('u');
    out.push_back('n');
    out.push_back('a');
    out.push_back('m');
    w32(3);
    const char* name = "TestUnit";
    for (const char* p = name; *p; p++)
        out.push_back(*p);
    out.push_back(0);
    while (out.size() % 4 != 0)
        out.push_back(0);
    // Mod 2: "uhab" (health), type=0 (int), value=500
    out.push_back('u');
    out.push_back('h');
    out.push_back('a');
    out.push_back('b');
    w32(0);
    w32(500);
    w32(0); // custom count
    return out;
}

static std::string write_temp(const std::vector<unsigned char>& data) {
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    lstrcatA(tmp, "ydwe_test_w3u.bin");
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return "";
    DWORD w;
    WriteFile(h, data.data(), (DWORD)data.size(), &w, NULL);
    CloseHandle(h);
    return std::string(tmp);
}

static std::string write_temp_named(const std::vector<unsigned char>& data, const char* name) {
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    lstrcatA(tmp, name);
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return "";
    DWORD w;
    WriteFile(h, data.data(), (DWORD)data.size(), &w, NULL);
    CloseHandle(h);
    return std::string(tmp);
}

static std::vector<unsigned char> make_real_layout_w3t() {
    std::vector<unsigned char> out;
    auto w32 = [&](DWORD v) {
        for (int i = 0; i < 4; i++)
            out.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
    };
    auto id = [&](const char* s) {
        for (int i = 0; i < 4; ++i)
            out.push_back((unsigned char)(s[i] ? s[i] : 0));
    };
    auto z = [&](const char* s) {
        for (const char* p = s; *p; ++p)
            out.push_back((unsigned char)*p);
        out.push_back(0);
    };

    w32(2);
    w32(0);
    w32(1);
    id("tstr");
    id("I004");
    w32(2);
    id("unam");
    w32(3);
    z("RealItem");
    w32(0);
    id("igol");
    w32(0);
    w32(123);
    w32(0);
    return out;
}

static std::vector<unsigned char> make_real_layout_w3a() {
    std::vector<unsigned char> out;
    auto w32 = [&](DWORD v) {
        for (int i = 0; i < 4; i++)
            out.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
    };
    auto id = [&](const char* s) {
        for (int i = 0; i < 4; ++i)
            out.push_back((unsigned char)(s[i] ? s[i] : 0));
    };

    w32(2);
    w32(0);
    w32(1);
    id("AHbz");
    id("A000");
    w32(1);
    id("Hbz1");
    w32(0);
    w32(1);
    w32(0);
    w32(50);
    w32(0);
    return out;
}

static std::vector<unsigned char> make_real_layout_object(
    const char* parent_id,
    const char* object_id,
    const char* field_id,
    const char* value,
    bool has_level
) {
    std::vector<unsigned char> out;
    auto w32 = [&](DWORD v) {
        for (int i = 0; i < 4; i++)
            out.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
    };
    auto id = [&](const char* s) {
        for (int i = 0; i < 4; ++i)
            out.push_back((unsigned char)(s[i] ? s[i] : 0));
    };
    auto z = [&](const char* s) {
        for (const char* p = s; *p; ++p)
            out.push_back((unsigned char)*p);
        out.push_back(0);
    };

    w32(2);
    w32(0);
    w32(1);
    id(parent_id);
    id(object_id);
    w32(1);
    id(field_id);
    w32(3);
    if (has_level) {
        w32(1);
        w32(0);
    }
    z(value);
    w32(0);
    return out;
}

TEST_CASE("ObjectAPI — w3u binary read", "[object][binary]") {
    auto blob = make_minimal_w3u();
    auto path = write_temp(blob);
    REQUIRE(!path.empty());

    const char* json = ydt_read_object_file(path.c_str());
    REQUIRE(json != nullptr);

    std::string j(json);
    // Debug: print raw JSON on failure
    INFO("JSON output: " << j);
    REQUIRE(j.find("\"magic\":\"W3U") != std::string::npos);
    REQUIRE(j.find("\"version\":1") != std::string::npos);
    REQUIRE(j.find("\"id\":\"hfoo\"") != std::string::npos);
    REQUIRE(j.find("\"unam\":\"TestUnit\"") != std::string::npos);
    REQUIRE(j.find("\"uhab\":500") != std::string::npos);

    DeleteFileA(path.c_str());
}

TEST_CASE("ObjectAPI — roundtrip (binary→JSON→binary→JSON)", "[object][roundtrip]") {
    auto blob = make_minimal_w3u();
    auto path = write_temp(blob);
    REQUIRE(!path.empty());

    // Read → JSON
    const char* json1 = ydt_read_object_file(path.c_str());
    REQUIRE(json1 != nullptr);
    std::string j1s(json1);
    INFO("Roundtrip JSON length: " << j1s.size());
    // Check JSON is well-formed (basic check)
    REQUIRE(j1s.size() > 10);
    REQUIRE(j1s[j1s.size() - 1] == '}');

    // JSON → Write back
    int wr = ydt_write_object_file(path.c_str(), json1);
    INFO("Write result: " << wr);
    REQUIRE(wr == 1);

    // Read again → JSON
    const char* json2 = ydt_read_object_file(path.c_str());
    REQUIRE(json2 != nullptr);
    INFO("Re-read JSON: " << json2);

    // JSONs should be identical
    REQUIRE(std::string(json1) == std::string(json2));

    DeleteFileA(path.c_str());
}

TEST_CASE("ObjectAPI — custom objects", "[object][custom]") {
    std::vector<unsigned char> out;
    auto w32 = [&](DWORD v) {
        for (int i = 0; i < 4; i++)
            out.push_back((unsigned char)((v >> (i * 8)) & 0xFF));
    };

    out.push_back('W');
    out.push_back('3');
    out.push_back('U');
    out.push_back(0);
    w32(1); // version
    w32(0); // 0 original
    w32(1); // 1 custom
    // Custom: id="h000" base="hfoo"
    out.push_back('h');
    out.push_back('0');
    out.push_back('0');
    out.push_back('0');
    out.push_back('h');
    out.push_back('f');
    out.push_back('o');
    out.push_back('o');
    w32(1); // 1 modification
    out.push_back('u');
    out.push_back('n');
    out.push_back('a');
    out.push_back('m');
    w32(3);
    const char* name = "CustomUnit";
    for (const char* p = name; *p; p++)
        out.push_back(*p);
    out.push_back(0);
    while (out.size() % 4 != 0)
        out.push_back(0);

    auto path = write_temp(out);
    REQUIRE(!path.empty());

    const char* json = ydt_read_object_file(path.c_str());
    REQUIRE(json != nullptr);

    std::string j(json);
    INFO("Custom JSON: " << j << " (len=" << j.size() << ")");
    REQUIRE(j.find("\"id\":\"h000\"") != std::string::npos);
    REQUIRE(j.find("\"base\":\"hfoo\"") != std::string::npos);
    REQUIRE(j.find("\"unam\":\"CustomUnit\"") != std::string::npos);
    REQUIRE(j.find("\"original\":[]") != std::string::npos);

    DeleteFileA(path.c_str());
}

TEST_CASE("ObjectAPI — write failure cases", "[object][error]") {
    // NULL path
    REQUIRE(ydt_write_object_file(nullptr, "{}") == 0);
    // NULL data
    REQUIRE(ydt_write_object_file("test.w3u", nullptr) == 0);

    // Invalid JSON
    REQUIRE(ydt_write_object_file("test.w3u", "not json") == 0);
    REQUIRE(ydt_write_object_file("test.w3u", "") == 0);
}

TEST_CASE("ObjectAPI — read failure cases", "[object][error]") {
    REQUIRE(ydt_read_object_file(nullptr) == nullptr);
    REQUIRE(ydt_read_object_file("nonexistent_file_12345.w3u") == nullptr);

    auto bad_tail = make_real_layout_w3t();
    bad_tail.resize(bad_tail.size() - 1);
    auto bad_tail_path = write_temp_named(bad_tail, "ydwe_test_bad_tail.w3t");
    REQUIRE(!bad_tail_path.empty());
    REQUIRE(ydt_read_object_file(bad_tail_path.c_str()) == nullptr);
    DeleteFileA(bad_tail_path.c_str());

    auto short_legacy = make_minimal_w3u();
    short_legacy.resize(12); // truncate after header
    auto short_path = write_temp_named(short_legacy, "ydwe_test_short.w3u");
    REQUIRE(!short_path.empty());
    REQUIRE(ydt_read_object_file(short_path.c_str()) == nullptr);
    DeleteFileA(short_path.c_str());
}

TEST_CASE("ObjectAPI real object layout without level data", "[object][real]") {
    auto blob = make_real_layout_w3t();
    auto path = write_temp_named(blob, "ydwe_test_real_layout.w3t");
    REQUIRE(!path.empty());

    const char* json = ydt_read_object_file(path.c_str());
    REQUIRE(json != nullptr);

    std::string j(json);
    INFO("Real w3t JSON: " << j);
    REQUIRE(j.find("\"version\":2") != std::string::npos);
    REQUIRE(j.find("\"id\":\"I004\"") != std::string::npos);
    REQUIRE(j.find("\"unam\":\"RealItem\"") != std::string::npos);
    REQUIRE(j.find("\"igol\":123") != std::string::npos);
    REQUIRE(j.find("\"field_details\"") != std::string::npos);

    REQUIRE(ydt_write_object_file(path.c_str(), json) == 1);
    const char* json2 = ydt_read_object_file(path.c_str());
    REQUIRE(json2 != nullptr);
    REQUIRE(std::string(json2).find("\"id\":\"I004\"") != std::string::npos);

    DeleteFileA(path.c_str());
}

TEST_CASE("ObjectAPI real object layout with level data", "[object][real][level]") {
    auto blob = make_real_layout_w3a();
    auto path = write_temp_named(blob, "ydwe_test_real_layout.w3a");
    REQUIRE(!path.empty());

    const char* json = ydt_read_object_file(path.c_str());
    REQUIRE(json != nullptr);

    std::string j(json);
    INFO("Real w3a JSON: " << j);
    REQUIRE(j.find("\"id\":\"A000\"") != std::string::npos);
    REQUIRE(j.find("\"Hbz1\":50") != std::string::npos);
    REQUIRE(j.find("\"level\":1") != std::string::npos);
    REQUIRE(j.find("\"data\":0") != std::string::npos);

    REQUIRE(ydt_write_object_file(path.c_str(), json) == 1);
    const char* json2 = ydt_read_object_file(path.c_str());
    REQUIRE(json2 != nullptr);
    std::string j2(json2);
    REQUIRE(j2.find("\"Hbz1\":50") != std::string::npos);
    REQUIRE(j2.find("\"level\":1") != std::string::npos);

    DeleteFileA(path.c_str());
}

TEST_CASE("ObjectAPI real layout covers every object editor file type", "[object][real][types]") {
    struct Case {
        const char* label;
        const char* suffix;
        const char* parent_id;
        const char* object_id;
        bool has_level;
    };
    const Case cases[] = {
        {"unit", "w3u", "hfoo", "U000", false},
        {"item", "w3t", "tstr", "I000", false},
        {"destructable", "w3b", "LTlt", "B000", false},
        {"doodad", "w3d", "D000", "D001", true},
        {"ability", "w3a", "AHbz", "A000", true},
        {"buff", "w3h", "BHbn", "H000", false},
        {"upgrade", "w3q", "R000", "R001", true},
    };

    for (const auto& tc : cases) {
        std::string file_name = std::string("ydwe_test_real_layout_") + tc.label + "." + tc.suffix;
        std::string value = std::string("Name_") + tc.label;
        auto blob = make_real_layout_object(tc.parent_id, tc.object_id, "unam", value.c_str(), tc.has_level);
        auto path = write_temp_named(blob, file_name.c_str());
        REQUIRE(!path.empty());

        const char* json = ydt_read_object_file(path.c_str());
        INFO("Object type: " << tc.label);
        REQUIRE(json != nullptr);
        std::string j(json);
        REQUIRE(j.find(std::string("\"id\":\"") + tc.object_id + "\"") != std::string::npos);
        REQUIRE(j.find(std::string("\"unam\":\"") + value + "\"") != std::string::npos);
        REQUIRE(j.find(tc.has_level ? "\"level\":1" : "\"level\":0") != std::string::npos);

        REQUIRE(ydt_write_object_file(path.c_str(), json) == 1);
        const char* json2 = ydt_read_object_file(path.c_str());
        REQUIRE(json2 != nullptr);
        REQUIRE(std::string(json2).find(std::string("\"id\":\"") + tc.object_id + "\"") != std::string::npos);
        REQUIRE(std::string(json2).find(std::string("\"unam\":\"") + value + "\"") != std::string::npos);

        DeleteFileA(path.c_str());
    }
}
