#include "highscores.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <windows.h>

static std::wstring utf8_to_wstring(const std::string &s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (needed <= 0) return {};
    std::wstring out; out.resize(needed);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], needed);
    return out;
}

static std::string wstring_to_utf8(const std::wstring &s) {
    if (s.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return {};
    std::string out; out.resize(needed);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], needed, NULL, NULL);
    return out;
}

static std::wstring trim(const std::wstring &s) {
    size_t a = 0; while (a < s.size() && iswspace(s[a])) ++a;
    size_t b = s.size(); while (b > a && iswspace(s[b-1])) --b;
    return s.substr(a, b - a);
}

std::vector<HighScoreEntry> HighScores::load(const std::wstring &path, size_t maxEntries) {
    std::vector<HighScoreEntry> out;
    std::ifstream ifs;
    std::string p = wstring_to_utf8(path);
    ifs.open(p, std::ios::binary);
    if (!ifs) return out;
    std::string raw;
    while (std::getline(ifs, raw)) {
        std::wstring line = utf8_to_wstring(raw);
        line = trim(line);
        if (line.empty()) continue;
        // Very tiny JSON-ish parser expecting lines like: {"name":"Player","score":123}
        size_t npos = line.find(L"\"name\"");
        size_t spos = line.find(L"\"score\"");
        if (npos==std::wstring::npos || spos==std::wstring::npos) continue;
        size_t q1 = line.find(L'"', npos + 6);
        size_t q2 = line.find(L'"', q1 + 1);
        if (q1==std::wstring::npos || q2==std::wstring::npos) continue;
        std::wstring name = line.substr(q1+1, q2-q1-1);
        size_t colon = line.find(L':', spos);
        if (colon==std::wstring::npos) continue;
        size_t numStart = colon+1;
        while (numStart < line.size() && iswspace(line[numStart])) ++numStart;
        int score = 0;
        try { score = std::stoi(std::wstring(line.begin()+numStart, line.end())); } catch(...) { continue; }
        out.push_back({name, score});
        if (out.size() >= maxEntries) break;
    }
    std::sort(out.begin(), out.end(), [](const HighScoreEntry &a, const HighScoreEntry &b){return a.score > b.score;});
    if (out.size() > maxEntries) out.resize(maxEntries);
    return out;
}

bool HighScores::save(const std::wstring &path, const std::vector<HighScoreEntry> &list) {
    std::ofstream ofs;
    std::string p2 = wstring_to_utf8(path);
    ofs.open(p2, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << "[\n";
    for (size_t i=0;i<list.size();++i) {
        const auto &e = list[i];
        // escape quotes in name
        std::wstring name = e.name;
        std::wstring esc;
        for (wchar_t ch : name) {
            if (ch == L'"') esc += L"\\\"";
            else esc += ch;
        }
        std::string utf = wstring_to_utf8(esc);
        ofs << "  {\"name\":\"" << utf << "\",\"score\":" << e.score << "}";
        if (i+1<list.size()) ofs << ",";
        ofs << "\n";
    }
    ofs << "]\n";
    return true;
}

std::vector<HighScoreEntry> HighScores::add_and_get(const std::wstring &path, const std::wstring &name, int score, size_t maxEntries) {
    auto list = load(path, maxEntries);
    list.push_back({name, score});
    std::sort(list.begin(), list.end(), [](const HighScoreEntry &a, const HighScoreEntry &b){return a.score > b.score;});
    if (list.size() > maxEntries) list.resize(maxEntries);
    save(path, list);
    return list;
}
