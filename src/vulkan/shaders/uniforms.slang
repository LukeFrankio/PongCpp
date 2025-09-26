/**
 * @file uniforms.slang
 * @brief Uniform buffer layouts for PongCpp Vulkan shaders
 * 
 * This module defines uniform buffer structures and bindings used
 * throughout the PongCpp shader system. Provides transformation
 * matrices, game state, and rendering parameters.
 */

import vertex_types;

/**
 * @brief Global transformation uniforms
 * 
 * Contains matrices and parameters needed for vertex transformation
 * from game coordinates to screen space.
 */
[[vk::binding(0, 0)]]
cbuffer GlobalUniforms {
    float4x4 projectionMatrix;       ///< Orthographic projection matrix
    float4x4 viewMatrix;             ///< View transformation matrix (usually identity for 2D)
    float4x4 modelMatrix;            ///< Model transformation matrix
    
    float2 screenSize;               ///< Screen dimensions in pixels
    float2 gameSize;                 ///< Game world dimensions
    
    float time;                      ///< Current time in seconds
    float deltaTime;                 ///< Frame delta time
    float2 _padding0;                ///< Alignment padding
}

/**
 * @brief Per-object uniforms
 * 
 * Contains per-object transformation and material properties.
 * Updated for each rendered object.
 */
[[vk::binding(1, 0)]]
cbuffer ObjectUniforms {
    float4x4 objectTransform;        ///< Object-specific transformation
    
    Material material;               ///< Material properties
    ShapeParams shape;               ///< Shape-specific parameters
    EffectParams effects;            ///< Visual effect parameters
    
    float3 _padding1;                ///< Alignment padding
}

/**
 * @brief Game state uniforms
 * 
 * Contains current game state information for dynamic effects
 * and gameplay-related rendering.
 */
[[vk::binding(2, 0)]]
cbuffer GameStateUniforms {
    float2 ballPosition;             ///< Current ball position
    float2 ballVelocity;             ///< Current ball velocity
    
    float2 leftPaddlePos;            ///< Left paddle position
    float2 rightPaddlePos;           ///< Right paddle position
    
    int scoreLeft;                   ///< Left player score
    int scoreRight;                  ///< Right player score
    
    float gameSpeed;                 ///< Game speed multiplier
    float aiDifficulty;              ///< AI difficulty level
    float2 _padding2;                ///< Alignment padding
};

/**
 * @brief Rendering settings uniforms
 * 
 * Contains rendering quality and effect settings that can be
 * adjusted at runtime for performance or visual quality.
 */
[[vk::binding(3, 0)]]
cbuffer RenderingUniforms {
    float antiAliasingStrength;      ///< Anti-aliasing intensity
    float glowStrength;              ///< Global glow effect strength
    float contrastLevel;             ///< Display contrast adjustment
    float brightnessLevel;           ///< Display brightness adjustment
    
    float shadowStrength;            ///< Shadow effect intensity
    float reflectionStrength;        ///< Reflection effect intensity
    float particleIntensity;         ///< Particle effect intensity
    float _padding3;                 ///< Alignment padding
};

/**
 * @brief Utility functions for uniform access
 */

/**
 * @brief Get combined model-view-projection matrix
 * @return MVP matrix for vertex transformation
 */
float4x4 getMVPMatrix() {
    return mul(mul(projectionMatrix, viewMatrix), modelMatrix);
}

/**
 * @brief Get combined object transformation matrix
 * @return Combined transformation including object-specific transform
 */
float4x4 getObjectMVPMatrix() {
    return mul(getMVPMatrix(), objectTransform);
}



/**
 * @brief Get time-based animation factor
 * @param speed Animation speed multiplier
 * @param offset Phase offset
 * @return Animated value between 0 and 1
 */
float getAnimationFactor(float speed, float offset = 0.0) {
    return (sin(time * speed + offset) + 1.0) * 0.5;
}

/**
 * @brief Get pulsing glow intensity based on game events
 * @return Glow intensity multiplier
 */
float getGameGlowIntensity() {
    // Simple pulse effect based on time only
    float pulseIntensity = getAnimationFactor(2.0);
    
    return clamp(1.0 + pulseIntensity * 0.5, 0.0, 2.0);
}
