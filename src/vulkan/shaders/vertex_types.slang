/**
 * @file vertex_types.slang
 * @brief Shared vertex and data structures for PongCpp Vulkan shaders
 * 
 * This module defines common data structures used across all shaders
 * in the PongCpp Vulkan renderer. Provides vertex input/output layouts
 * and shared utility structures.
 */

/**
 * @brief Vertex input structure from vertex buffers
 */
struct VertexInput {
    float2 position : POSITION;      ///< Vertex position in game coordinates
    float3 color : COLOR0;           ///< Vertex color (RGB)
    float2 uv : TEXCOORD0;           ///< Texture coordinates
};

/**
 * @brief Vertex shader output / Fragment shader input
 */
struct VertexOutput {
    float4 position : SV_Position;   ///< Clip space position
    float3 color : COLOR0;           ///< Interpolated color
    float2 uv : TEXCOORD0;           ///< Interpolated texture coordinates
    float2 worldPos : TEXCOORD1;     ///< World space position (for effects)
};

/**
 * @brief Fragment shader output
 */
struct FragmentOutput {
    float4 color : SV_Target0;       ///< Final pixel color
};

/**
 * @brief Material properties for different primitive types
 */
struct Material {
    float3 baseColor;                ///< Base material color
    float roughness;                 ///< Surface roughness (0 = smooth, 1 = rough)
    float metallic;                  ///< Metallic factor (0 = dielectric, 1 = metallic)
    float emission;                  ///< Emission intensity
};

/**
 * @brief Effect parameters for visual enhancements
 */
struct EffectParams {
    float glowIntensity;             ///< Glow effect intensity
    float glowRadius;                ///< Glow effect radius
    float pulseSpeed;                ///< Pulse animation speed
    float time;                      ///< Current time for animations
};

/**
 * @brief Primitive type identifiers
 */
enum PrimitiveType : uint {
    PRIMITIVE_RECTANGLE = 0,
    PRIMITIVE_CIRCLE = 1,
    PRIMITIVE_LINE = 2,
    PRIMITIVE_TEXT = 3
};

/**
 * @brief Shape parameters for different primitive types
 */
struct ShapeParams {
    float2 size;                     ///< Shape dimensions
    float cornerRadius;              ///< Corner radius for rounded rectangles
    float thickness;                 ///< Line thickness or border width
    PrimitiveType type;              ///< Primitive type identifier
};

/**
 * @brief Utility functions for vertex processing
 */

/**
 * @brief Transform position from game coordinates to normalized device coordinates
 * @param gamePos Position in game coordinate system
 * @param gameSize Game world dimensions
 * @return Position in NDC space (-1 to +1)
 */
float2 gameToNDC(float2 gamePos, float2 gameSize) {
    return (gamePos / gameSize) * 2.0 - 1.0;
}


