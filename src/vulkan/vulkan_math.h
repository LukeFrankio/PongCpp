/**
 * @file vulkan_math.h
 * @brief Minimal 2D mathematics library for Vulkan renderer
 * 
 * This header provides essential 2D math types and functions needed for the
 * Vulkan Pong implementation. Designed to be lightweight and self-contained
 * without external dependencies like GLM.
 */

#pragma once

// Fix Windows.h min/max macro conflicts
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cmath>
#include <algorithm>

namespace VulkanMath {

// Force use of std versions to avoid Windows.h macro conflicts
using std::min;
using std::max;

/**
 * @brief 2D vector structure
 */
struct Vec2 {
    float x, y;
    
    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    
    // Vector operations
    Vec2 operator+(const Vec2& other) const { return {x + other.x, y + other.y}; }
    Vec2 operator-(const Vec2& other) const { return {x - other.x, y - other.y}; }
    Vec2 operator*(float scalar) const { return {x * scalar, y * scalar}; }
    Vec2 operator/(float scalar) const { return {x / scalar, y / scalar}; }
    
    Vec2& operator+=(const Vec2& other) { x += other.x; y += other.y; return *this; }
    Vec2& operator-=(const Vec2& other) { x -= other.x; y -= other.y; return *this; }
    Vec2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
    Vec2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }
    
    // Utility functions
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }
    Vec2 normalized() const { 
        float len = length(); 
        return len > 0.0f ? *this / len : Vec2(0.0f, 0.0f);
    }
    float dot(const Vec2& other) const { return x * other.x + y * other.y; }
};

/**
 * @brief 3D vector structure (for colors)
 */
struct Vec3 {
    float x, y, z;
    
    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3(float rgb) : x(rgb), y(rgb), z(rgb) {}
    
    // Color operations
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
};

/**
 * @brief 4x4 matrix structure for transformations
 * 
 * Matrix is stored in column-major order for Vulkan compatibility.
 * Matrix[col][row] indexing convention.
 */
struct Mat4 {
    std::array<std::array<float, 4>, 4> m;
    
    Mat4() {
        // Initialize to identity matrix
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                m[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
    }
    
    // Access operators
    std::array<float, 4>& operator[](int col) { return m[col]; }
    const std::array<float, 4>& operator[](int col) const { return m[col]; }
    
    // Get raw data pointer for Vulkan
    const float* data() const { return &m[0][0]; }
    float* data() { return &m[0][0]; }
    
    // Matrix multiplication
    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                result[col][row] = 0.0f;
                for (int k = 0; k < 4; k++) {
                    result[col][row] += m[k][row] * other[col][k];
                }
            }
        }
        return result;
    }
};

/**
 * @brief Vertex data structure for rendering
 */
struct Vertex {
    Vec2 position;    ///< Vertex position in game coordinates
    Vec3 color;       ///< Vertex color (RGB)
    Vec2 uv;          ///< Texture coordinates (if needed)
    
    Vertex() = default;
    Vertex(Vec2 pos, Vec3 col) : position(pos), color(col), uv(0.0f, 0.0f) {}
    Vertex(Vec2 pos, Vec3 col, Vec2 texCoord) : position(pos), color(col), uv(texCoord) {}
};

// ============================================================================
// Transformation Functions
// ============================================================================

/**
 * @brief Create orthographic projection matrix
 * @param left Left edge of projection
 * @param right Right edge of projection  
 * @param bottom Bottom edge of projection
 * @param top Top edge of projection
 * @param nearPlane Near clipping plane
 * @param farPlane Far clipping plane
 * @return Orthographic projection matrix
 */
inline Mat4 orthographicProjection(
    float left, float right, 
    float bottom, float top,
    float nearPlane = -1.0f, float farPlane = 1.0f) {
    
    Mat4 result;
    
    // Column 0
    result[0][0] = 2.0f / (right - left);
    result[0][1] = 0.0f;
    result[0][2] = 0.0f;
    result[0][3] = 0.0f;
    
    // Column 1  
    result[1][0] = 0.0f;
    result[1][1] = 2.0f / (top - bottom);
    result[1][2] = 0.0f;
    result[1][3] = 0.0f;
    
    // Column 2
    result[2][0] = 0.0f;
    result[2][1] = 0.0f;
    result[2][2] = -2.0f / (farPlane - nearPlane);
    result[2][3] = 0.0f;
    
    // Column 3 (translation)
    result[3][0] = -(right + left) / (right - left);
    result[3][1] = -(top + bottom) / (top - bottom);
    result[3][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    result[3][3] = 1.0f;
    
    return result;
}

/**
 * @brief Create 2D translation matrix
 * @param offset Translation offset
 * @return Translation matrix
 */
inline Mat4 translation(Vec2 offset) {
    Mat4 result;
    result[3][0] = offset.x;
    result[3][1] = offset.y;
    return result;
}

/**
 * @brief Create 2D scaling matrix
 * @param scale Scaling factors
 * @return Scaling matrix
 */
inline Mat4 scaling(Vec2 scale) {
    Mat4 result;
    result[0][0] = scale.x;
    result[1][1] = scale.y;
    return result;
}

/**
 * @brief Create 2D rotation matrix
 * @param angleRadians Rotation angle in radians
 * @return Rotation matrix
 */
inline Mat4 rotation2D(float angleRadians) {
    Mat4 result;
    float c = std::cos(angleRadians);
    float s = std::sin(angleRadians);
    
    result[0][0] = c;
    result[0][1] = s;
    result[1][0] = -s;
    result[1][1] = c;
    
    return result;
}

// ============================================================================
// Game Coordinate Transformations
// ============================================================================

/**
 * @brief Transform point from game coordinates to normalized device coordinates
 * @param gamePos Position in game coordinate system (0-80, 0-24)
 * @param gameWidth Game world width (typically 80)
 * @param gameHeight Game world height (typically 24)
 * @return Position in NDC (-1 to +1)
 */
inline Vec2 gameToNDC(Vec2 gamePos, float gameWidth, float gameHeight) {
    return Vec2(
        (gamePos.x / gameWidth) * 2.0f - 1.0f,
        (gamePos.y / gameHeight) * 2.0f - 1.0f
    );
}

/**
 * @brief Transform point from screen coordinates to game coordinates
 * @param screenPos Position in screen pixels
 * @param screenWidth Screen width in pixels
 * @param screenHeight Screen height in pixels
 * @param gameWidth Game world width
 * @param gameHeight Game world height
 * @return Position in game coordinates
 */
inline Vec2 screenToGame(
    Vec2 screenPos, 
    float screenWidth, float screenHeight,
    float gameWidth, float gameHeight) {
    
    return Vec2(
        (screenPos.x / screenWidth) * gameWidth,
        (screenPos.y / screenHeight) * gameHeight
    );
}

/**
 * @brief Create projection matrix for game coordinate system
 * @param gameWidth Game world width
 * @param gameHeight Game world height
 * @return Projection matrix that maps game coords to NDC
 */
inline Mat4 gameProjectionMatrix(float gameWidth, float gameHeight) {
    // Vulkan has inverted Y-axis compared to OpenGL, so flip top/bottom
    return orthographicProjection(0.0f, gameWidth, gameHeight, 0.0f);
}

// ============================================================================
// Color Utilities
// ============================================================================

/**
 * @brief Convert RGB values (0-255) to normalized color
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return Normalized RGB color (0-1)
 */
inline Vec3 rgb(int r, int g, int b) {
    return Vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

/**
 * @brief Common colors for Pong game
 */
namespace Colors {
    const Vec3 WHITE = Vec3(1.0f, 1.0f, 1.0f);
    const Vec3 BLACK = Vec3(0.0f, 0.0f, 0.0f);
    const Vec3 PADDLE = rgb(240, 240, 240);     // Light gray paddles
    const Vec3 BALL = rgb(250, 220, 220);       // Light pink ball
    const Vec3 BALL_CORE = rgb(200, 80, 80);    // Dark red ball core
    const Vec3 LINE = rgb(200, 200, 200);       // Center line
    const Vec3 LINE_GLOW = rgb(100, 100, 120);  // Center line glow
    const Vec3 BACKGROUND = Vec3(0.0f);         // Black background
}

// ============================================================================
// Geometry Generation Helpers
// ============================================================================

/**
 * @brief Generate vertices for a rectangle
 * @param center Rectangle center position
 * @param size Rectangle dimensions
 * @param color Rectangle color
 * @return Array of 6 vertices (2 triangles)
 */
inline std::array<Vertex, 6> generateRectangle(
    Vec2 center, Vec2 size, Vec3 color) {
    
    Vec2 halfSize = size * 0.5f;
    Vec2 topLeft = center + Vec2(-halfSize.x, -halfSize.y);
    Vec2 topRight = center + Vec2(halfSize.x, -halfSize.y);
    Vec2 bottomLeft = center + Vec2(-halfSize.x, halfSize.y);
    Vec2 bottomRight = center + Vec2(halfSize.x, halfSize.y);
    
    return {
        // First triangle
        Vertex(topLeft, color),
        Vertex(bottomLeft, color),
        Vertex(topRight, color),
        // Second triangle
        Vertex(topRight, color),
        Vertex(bottomLeft, color),
        Vertex(bottomRight, color)
    };
}

/**
 * @brief Calculate distance from point to rectangle edge (for rounded corners)
 * @param point Point to test
 * @param center Rectangle center
 * @param size Rectangle size
 * @return Distance to edge (negative = inside)
 */
inline float distanceToRectangle(Vec2 point, Vec2 center, Vec2 size) {
    Vec2 offset = Vec2(std::abs(point.x - center.x), std::abs(point.y - center.y));
    Vec2 halfSize = size * 0.5f;
    Vec2 excess = Vec2(
        max(0.0f, offset.x - halfSize.x),
        max(0.0f, offset.y - halfSize.y)
    );
    return excess.length();
}

} // namespace VulkanMath