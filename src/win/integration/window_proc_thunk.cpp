#include "window_proc_thunk.h"
LRESULT CALLBACK RefactorWindowProc(HWND h, UINT m, WPARAM w, LPARAM l){ return DefWindowProc(h,m,w,l); }
