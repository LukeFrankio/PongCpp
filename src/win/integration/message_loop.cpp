#include "message_loop.h"
#include <windows.h>
#include "../app/app_controller.h"
#include "../platform/win_window.h"
int run_message_loop(WinWindow& win, AppController& app){ MSG msg; while(true){ while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){ if(msg.message==WM_QUIT) return 0; TranslateMessage(&msg); DispatchMessage(&msg);} app.tick(0.0); app.render(); Sleep(16);} }
