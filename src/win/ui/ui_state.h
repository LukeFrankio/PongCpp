#pragma once
enum class UIMode { Gameplay, MainMenu, NameEntry, Settings };
struct UIState { UIMode mode = UIMode::Gameplay; int highlight = -1; bool redraw=true; };
