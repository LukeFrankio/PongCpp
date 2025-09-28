#pragma once
#include <vector>
#include <string>
struct HighScoreEntry;
class HighScoresStore { public: const std::vector<HighScoreEntry>& list() const; void add(const std::wstring&, int score); bool load(const std::wstring&); private: std::vector<HighScoreEntry> entries; };
