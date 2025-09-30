/**
 * @file backbuffer.h
 * @brief Off-screen rendering buffer for flicker-free drawing
 * 
 * This file provides the BackBuffer class that creates an off-screen
 * GDI bitmap for smooth, flicker-free rendering in Windows applications.
 */

#pragma once
#include <windows.h>

/**
 * @brief Off-screen GDI rendering buffer
 * 
 * BackBuffer creates and manages an off-screen Windows GDI bitmap
 * that can be used for double-buffered rendering. This eliminates
 * screen flicker by allowing the application to draw everything
 * to the off-screen buffer first, then copy the complete image
 * to the screen in a single operation.
 * 
 * The buffer automatically manages its own device context and
 * handles cleanup of GDI resources when destroyed.
 */
class BackBuffer {
public:
    /**
     * @brief Construct an empty BackBuffer
     * 
     * Creates an uninitialized buffer. Call resize() before use.
     */
    BackBuffer() = default;
    
    /**
     * @brief Destroy the BackBuffer and clean up GDI resources
     * 
     * Automatically releases all allocated GDI objects including
     * the memory device context and bitmap.
     */
    ~BackBuffer();
    
    /**
     * @brief Resize the buffer to match the specified dimensions
     * 
     * Creates or recreates the off-screen bitmap to match the given
     * width and height. If the buffer already exists and is the same
     * size, no operation is performed.
     * 
     * @param screen Device context compatible with the target screen
     * @param w New width in pixels
     * @param h New height in pixels
     */
    void resize(HDC screen, int w, int h);
    
    /**
     * @brief Get the device context for drawing operations
     * 
     * Returns the memory device context that can be used with standard
     * GDI drawing functions. All drawing operations should target this DC.
     * 
     * @return Device context handle for the off-screen buffer
     */
    HDC dc() const { return memDC; }
    HBITMAP getBitmap() const { return bmp; }
    
private:
    HDC memDC = nullptr;      ///< Memory device context for off-screen drawing
    HBITMAP bmp = nullptr;    ///< Off-screen bitmap handle  
    HBITMAP oldBmp = nullptr; ///< Previously selected bitmap (for restoration)
    int W = 0;                ///< Current buffer width in pixels
    int H = 0;                ///< Current buffer height in pixels
};
