/**
 * @file test_minimal.slang
 * @brief Minimal test shader without uniform buffers to isolate rendering issues
 */

/**
 * @brief Minimal vertex input structure (must match C++ Vertex struct layout)
 */
struct MinimalVertexInput {
    float2 position : POSITION;      ///< Vertex position in game coordinates
    float3 color : COLOR0;           ///< Vertex color (RGB)
    float2 uv : TEXCOORD0;           ///< Texture coordinates (unused but needed for layout)
};

/**
 * @brief Minimal vertex shader output
 */
struct MinimalVertexOutput {
    float4 position : SV_Position;   ///< Clip space position
    float3 color : COLOR0;           ///< Interpolated color
    float2 uv : TEXCOORD0;           ///< Pass-through UV (matches C++ vertex layout)
};

/**
 * @brief Minimal fragment shader output
 */
struct MinimalFragmentOutput {
    float4 color : SV_Target0;       ///< Final pixel color
};

/**
 * @brief FIXED: Vertex shader that uses input positions with debugging
 * Uses actual vertex input positions without requiring DrawParameters capability
 */
[shader("vertex")]
MinimalVertexOutput vertexMinimal(MinimalVertexInput input) {
    MinimalVertexOutput output;
    
    // Use the actual vertex input positions (assumed NDC when testing minimal pipeline)
    output.position = float4(input.position.x, input.position.y, 0.5, 1.0);
    output.color = input.color; // Use the input color from vertices
    output.uv = input.uv;       // Pass through UV coordinates to fragment stage
    
    return output;
}

/**
 * @brief EXTREME DEBUG: Fragment shader creates checkerboard pattern impossible to miss
 */
[shader("fragment")]
MinimalFragmentOutput fragmentMinimal(MinimalVertexOutput input) {
    MinimalFragmentOutput output;
    
    // Create checkerboard based on UV coordinates so the pattern is visible when fragments exist
    float2 gridUV = input.uv * 32.0; // 32x32 grid
    int2 coord = int2(floor(gridUV));
    bool checker = ((coord.x + coord.y) % 2) == 0;
    
    if (checker) {
        // Bright red squares
        output.color = float4(1.0, 0.0, 0.0, 1.0);
    } else {
        // Bright yellow squares
        output.color = float4(1.0, 1.0, 0.0, 1.0);
    }
    
    return output;
}