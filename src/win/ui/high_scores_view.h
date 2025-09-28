#pragma once
#include <vector>
#include <string>
#include <windows.h> // for HDC
struct HighScoreEntry; struct UIState;
class HighScoresView {
public:
    void begin(std::vector<HighScoreEntry>* list); // non-owning
    // Returns false when user closes (handled externally); draws UI & deletion button.
    bool frame(HDC dc,int w,int h,int dpi, int mx=-1,int my=-1,bool click=false,bool deleteRequest=false, int* deletedIndex=nullptr);
private:
    std::vector<HighScoreEntry>* scores = nullptr; 
    int hoverIndex = -1;
};
