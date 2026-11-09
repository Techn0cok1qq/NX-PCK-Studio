// libcurl を偽物に差し替えて、Discordの応答JSON → inbox保存 を検証する
#include <curl/curl.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include "discord.hpp"

static std::string g_url; static size_t (*g_cb)(char*, size_t, size_t, void*); static void* g_ud; static bool g_auth = false;
static std::string rd(const char* p) { std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }

CURL* curl_easy_init() { g_auth = false; return (CURL*)1; }
curl_slist* curl_slist_append(curl_slist* l, const char* s) { if (strstr(s, "Authorization: Bot TOKEN")) g_auth = true; return (curl_slist*)1; }
CURLcode curl_easy_setopt(CURL*, int o, ...) {
    va_list a; va_start(a, o);
    if (o == CURLOPT_URL) g_url = va_arg(a, const char*);
    else if (o == CURLOPT_WRITEFUNCTION) g_cb = va_arg(a, decltype(g_cb));
    else if (o == CURLOPT_WRITEDATA) g_ud = va_arg(a, void*);
    va_end(a); return 0;
}
CURLcode curl_easy_perform(CURL*) {
    std::string body;
    if (g_url.find("/messages") != std::string::npos) {
        body = g_auth ? R"([{"id":"1003","attachments":[{"id":"2002","filename":"my cape.PNG","size":148,"url":"https://cdn.discordapp.com/attachments/1/2002/cape.PNG?ex=1&is=2&hm=3","proxy_url":"x"}]},
{"id":"1002","attachments":[{"id":"2001","filename":"skin.png","size":208,"url":"https://cdn.discordapp.com/attachments/1/2001/skin.png?ex=1\u0026is=2\u0026hm=3"}]},
{"id":"1001","attachments":[{"id":"2000","filename":"notes.txt","url":"https://cdn.discordapp.com/a/notes.txt"}]},{"id":"1000","attachments":[]}])" : "NOAUTH";
    } else {
        if (g_auth) body = "LEAKED_TOKEN_TO_CDN";                    // CDNにトークンを送ってはいけない
        else if (g_url.find("skin.png") != std::string::npos) body = rd("skin.png");
        else if (g_url.find("cape.PNG") != std::string::npos) body = rd("cape.png");
    }
    g_cb((char*)body.data(), 1, body.size(), g_ud);
    return 0;
}
CURLcode curl_easy_getinfo(CURL*, int o, ...) {
    va_list a; va_start(a, o); long* p = va_arg(a, long*); *p = 200; va_end(a); return 0;
}

void curl_slist_free_all(curl_slist*) {} void curl_easy_cleanup(CURL*) {} int curl_global_init(long) { return 0; } void curl_global_cleanup() {}

int main() {
    util::Config c; c.token = "TOKEN"; c.channel = "123";
    std::string err;
    int n = discord::sync(c, "/tmp/inbox_test/", err);
    printf("sync -> %d %s\n", n, err.c_str());
    int n2 = discord::sync(c, "/tmp/inbox_test/", err);
    printf("2nd sync (dedupe) -> %d\n", n2);
}
