// cd tests && g++ -std=c++17 -I../source -o test_update test_update.cpp && ./test_update
#include <cassert>
#include <cstdio>
#include "updcore.hpp"
using namespace updcore;
int main() {
    assert(isNewer("v1.0.1", "v1.0.0")); assert(isNewer("1.1.0", "v1.0.9")); assert(isNewer("v2.0", "1.9.9"));
    assert(isNewer("v1.0.10", "v1.0.9"));
    assert(!isNewer("v1.0.0", "v1.0.0")); assert(!isNewer("v0.9.9", "v1.0.0")); assert(!isNewer("latest", "v1.0.0")); assert(!isNewer("", "v1.0.0"));
    assert(!isNewer("v1.0.0-beta", "v1.0.0"));
    std::string js = R"({"tag_name":"v1.2.0","name":"NX PCK Studio 1.2.0","body":"## 変更\r\n- 更新機能\r\n- \u30a2\u30a4\u30b3\u30f3 \ud83d\ude00","draft":false,
      "assets":[{"name":"source.zip","browser_download_url":"https://x/s.zip","size":5},
                {"name":"nxpckstudio.nro","browser_download_url":"https://github.com/o/r/releases/download/v1.2.0/nxpckstudio.nro","size":3456789,
                 "digest":"sha256:ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef0123456789"}]})";
    Release r;
    assert(parseRelease(js, r));
    assert(r.tag == "v1.2.0" && r.hasAsset && r.assetName == "nxpckstudio.nro" && r.size == 3456789);
    assert(r.sha256 == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    assert(r.body.find("アイコン") != std::string::npos && r.body.find("\xF0\x9F\x98\x80") != std::string::npos);
    std::string sn = shortNotes(r.body);
    assert(sn.find("・更新機能") != std::string::npos && sn.find('#') == std::string::npos);
    Release r2;
    assert(parseRelease(R"({"tag_name":"v1.0.1","assets":[]})", r2) && !r2.hasAsset);
    assert(!parseRelease(R"({"message":"Not Found"})", r2));
    assert(!parseRelease("<html>", r2)); assert(!parseRelease(R"({"tag_name":"v1")", r2));
    std::string big(1000, 'a'); big = "\xE3\x81\x82" + big;
    assert(shortNotes(big).size() <= 245);
    puts("ok");
}
