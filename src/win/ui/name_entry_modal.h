#pragma once
#include <string>
class NameEntryModal { public: void start(); void feed_char(wchar_t); void backspace(); bool ready() const; std::wstring name() const; void draw(void* /*HDC*/); private: std::wstring buf; };
