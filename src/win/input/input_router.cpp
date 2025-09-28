#include "input_router.h"
#include <windowsx.h>
void InputRouter::handle(UINT m, WPARAM w, LPARAM l){
    switch(m){
    case WM_MOUSEMOVE: state.mx = GET_X_LPARAM(l); state.my = GET_Y_LPARAM(l); break;
    case WM_LBUTTONDOWN: state.mdown=true; break;
    case WM_LBUTTONUP: state.mdown=false; state.click=true; break;
    case WM_MOUSEWHEEL: state.wheel += GET_WHEEL_DELTA_WPARAM(w); break;
    case WM_KEYDOWN: if(w<256) state.keys[w]=true; break;
    case WM_KEYUP: if(w<256) state.keys[w]=false; break;
    }
}
