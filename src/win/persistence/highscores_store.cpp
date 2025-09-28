#include "highscores_store.h"
#include "../highscores.h"
const std::vector<HighScoreEntry>& HighScoresStore::list() const { return entries; }
void HighScoresStore::add(const std::wstring& n, int s){ entries.push_back({n,s}); }
bool HighScoresStore::load(const std::wstring&){ return true; }
