#include "name_entry_modal.h"
void NameEntryModal::start(){ buf.clear(); }
void NameEntryModal::feed_char(wchar_t c){ buf.push_back(c); }
void NameEntryModal::backspace(){ if(!buf.empty()) buf.pop_back(); }
bool NameEntryModal::ready() const { return !buf.empty(); }
std::wstring NameEntryModal::name() const { return buf; }
void NameEntryModal::draw(void*){}
