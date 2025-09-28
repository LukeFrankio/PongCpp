#pragma once
struct InputState {
	bool keys[256] = {false};
	bool prev[256] = {false}; // previous frame snapshot for edge detection
	int mx=0; int my=0; bool mdown=false; int wheel=0; bool click=false; 
	void advance(){ for(int i=0;i<256;i++) prev[i]=keys[i]; click=false; wheel=0; }
	bool is_pressed(int vk) const { return vk>=0 && vk<256 && keys[vk]; }
	bool just_pressed(int vk) const { return vk>=0 && vk<256 && keys[vk] && !prev[vk]; }
	bool just_released(int vk) const { return vk>=0 && vk<256 && !keys[vk] && prev[vk]; }
};
