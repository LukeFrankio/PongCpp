#pragma once
struct SliderWidget { int* value=nullptr; int min=0,max=100; void draw(void*); };
struct ButtonWidget { const char* label=""; bool clicked=false; void draw(void*); };
