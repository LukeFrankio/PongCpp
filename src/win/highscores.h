#pragma once
#include <string>
#include <vector>

struct HighScoreEntry { std::wstring name; int score; };

class HighScores {
public:
    HighScores() = default;
    std::vector<HighScoreEntry> load(const std::wstring &path, size_t maxEntries = 10);
    bool save(const std::wstring &path, const std::vector<HighScoreEntry> &list);
    std::vector<HighScoreEntry> add_and_get(const std::wstring &path, const std::wstring &name, int score, size_t maxEntries = 10);
};
