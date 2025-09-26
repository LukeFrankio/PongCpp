/**
 * @file primitives.slang
 * @brief Main shader module for primitive rendering in PongCpp
 * 
 * This module contains the vertex and fragment shaders for rendering
 * basic 2D primitives (rectangles, circles, lines) used in the Pong game.
 * Supports solid colors, gradients, and basic visual effects.
 */

import vertex_types;
import uniforms;

/**
 * @brief Transform position through full transformation pipeline
 * @param localPos Position in local object space
 * @return Position in clip space
 */
float4 transformPosition(float2 localPos) {
    // DUMMY FUNCTION - bypassed in vertex shader
    return float4(0.0, 0.0, 0.0, 1.0);
}

/**
 * @brief Calculate world position from local coordinates
 * @param localPos Position in local object space
 * @return Position in world space
 */
float2 getWorldPosition(float2 localPos) {
    // DUMMY FUNCTION - bypassed in vertex shader
    return float2(0.0, 0.0);
}

/**
 * @brief Calculate distance from point to rectangle (for rounded corners)
 * @param point Point to test
 * @param center Rectangle center
 * @param size Rectangle size
 * @return Distance to rectangle edge
 */
float distanceToRectangle(float2 point, float2 center, float2 size) {
    float2 offset = abs(point - center);
    float2 halfSize = size * 0.5;
    float2 excess = max(float2(0.0, 0.0), offset - halfSize);
    return length(excess);
}

/**
 * @brief Calculate distance from point to circle
 * @param point Point to test
 * @param center Circle center
 * @param radius Circle radius
 * @return Distance to circle edge (negative = inside)
 */
float distanceToCircle(float2 point, float2 center, float radius) {
    return length(point - center) - radius;
}

/**
 * @brief Smooth step function for anti-aliasing
 * @param edge0 Lower edge
 * @param edge1 Upper edge
 * @param x Input value
 * @return Smoothly interpolated value
 */
float smoothStep(float edge0, float edge1, float x) {
    float t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
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
    // Increase glow when ball is moving fast
    float velocityIntensity = length(ballVelocity) * 0.1;
    
    // Pulse effect
    float pulseIntensity = getAnimationFactor(effects.pulseSpeed);
    
    return clamp(glowStrength * (1.0 + velocityIntensity + pulseIntensity * 0.5), 0.0, 2.0);
}

/**
 * @brief Standard vertex shader for 2D primitives
 * 
 * Transforms input position from game coordinates to clip space using
 * the projection matrix from uniform buffer.
 */
[shader("vertex")]
VertexOutput vertexMain(VertexInput input) {
    VertexOutput output;
    // Transform the input position from game/object space into clip space using the
    // combined object/model-view-projection matrix supplied by the uniform buffers.
    // Use getObjectMVPMatrix() so per-object transforms (objectTransform) are honored.
    float4 worldPos4 = float4(input.position.x, input.position.y, 0.0, 1.0);
    // Compute full transform explicitly to avoid cross-module helper linking issues:
    // position_clip = projectionMatrix * viewMatrix * modelMatrix * objectTransform * position_local
    float4 tmp1 = mul(objectTransform, worldPos4);
    float4 tmp2 = mul(modelMatrix, tmp1);
    float4 tmp3 = mul(viewMatrix, tmp2);
    output.position = mul(projectionMatrix, tmp3);
    output.color = input.color;
    output.uv = input.uv;
    output.worldPos = input.position;
    return output;
}

/**
 * @brief Solid color fragment shader
 * 
 * Renders primitives with solid colors, supporting basic anti-aliasing
 * for smooth edges on rectangles and circles.
 */
[shader("fragment")]
FragmentOutput fragmentSolid(VertexOutput input) {
    FragmentOutput output;
    // Ignore uniforms for now and just show vertex color (opaque)
    output.color = float4(input.color, 1.0);
    return output;
}

/**
 * @brief Glow effect fragment shader
 * 
 * Renders primitives with glow effects, supporting dynamic intensity
 * based on game state and animation parameters.
 */
[shader("fragment")]
FragmentOutput fragmentGlow(VertexOutput input) {
    FragmentOutput output;
    
    float3 baseColor = input.color * material.baseColor;
    float alpha = 1.0;
    float glowFactor = 0.0;
    
    // Shape-specific processing with glow
    if (shape.type == PrimitiveType::PRIMITIVE_RECTANGLE) {
        float2 center = float2(0.5, 0.5);
        float2 size = float2(1.0, 1.0);
        
        float dist = distanceToRectangle(input.uv, center, size - shape.cornerRadius * 2.0);
        
        // Core shape
        alpha = 1.0 - smoothStep(0.0, antiAliasingStrength, dist - shape.cornerRadius);
        
        // Glow effect
        float glowDist = dist - shape.cornerRadius;
        glowFactor = exp(-glowDist * glowDist / (effects.glowRadius * effects.glowRadius));
    }
    else if (shape.type == PrimitiveType::PRIMITIVE_CIRCLE) {
        float2 center = float2(0.5, 0.5);
        float radius = 0.5;
        
        float dist = distanceToCircle(input.uv, center, radius);
        
        // Core shape
        alpha = 1.0 - smoothStep(-antiAliasingStrength, antiAliasingStrength, dist);
        
        // Glow effect
        glowFactor = exp(-dist * dist / (effects.glowRadius * effects.glowRadius));
    }
    
    // Combine base color with glow
    float glowIntensity = getGameGlowIntensity() * effects.glowIntensity;
    float3 glowColor = baseColor * 1.5; // Slightly brighter for glow
    
    float3 finalColor = lerp(baseColor, glowColor, glowFactor * glowIntensity);
    
    // Apply material properties
    finalColor += material.emission;
    alpha = max(alpha, glowFactor * glowIntensity * 0.3); // Extend alpha for glow
    
    output.color = float4(finalColor, alpha);
    return output;
}

/**
 * @brief Animated fragment shader
 * 
 * Renders primitives with time-based animations like pulsing,
 * color cycling, and other dynamic effects.
 */
[shader("fragment")]
FragmentOutput fragmentAnimated(VertexOutput input) {
    FragmentOutput output;
    
    float3 baseColor = input.color * material.baseColor;
    float alpha = 1.0;
    
    // Time-based color modulation
    float animFactor = getAnimationFactor(effects.pulseSpeed);
    float3 animatedColor = baseColor * (0.8 + 0.4 * animFactor);
    
    // Shape processing (same as solid)
    if (shape.type == PrimitiveType::PRIMITIVE_RECTANGLE) {
        if (shape.cornerRadius > 0.0) {
            float2 center = float2(0.5, 0.5);
            float2 size = float2(1.0, 1.0);
            
            float dist = distanceToRectangle(input.uv, center, size - shape.cornerRadius * 2.0);
            alpha = 1.0 - smoothStep(0.0, antiAliasingStrength, dist - shape.cornerRadius);
        }
    }
    else if (shape.type == PrimitiveType::PRIMITIVE_CIRCLE) {
        float2 center = float2(0.5, 0.5);
        float radius = 0.5;
        
        float dist = distanceToCircle(input.uv, center, radius);
        alpha = 1.0 - smoothStep(-antiAliasingStrength, antiAliasingStrength, dist);
    }
    
    // Apply animation and material
    float3 finalColor = animatedColor + material.emission * animFactor;
    
    output.color = float4(finalColor, alpha);
    return output;
}

/**
 * @brief Gradient fragment shader
 * 
 * Renders primitives with color gradients, useful for backgrounds
 * and special visual effects.
 */
[shader("fragment")]
FragmentOutput fragmentGradient(VertexOutput input) {
    FragmentOutput output;
    
    // Create gradient based on UV coordinates
    float gradientFactor = input.uv.y; // Vertical gradient
    
    // Interpolate between base color and a lighter/darker variant
    float3 color1 = input.color * material.baseColor;
    float3 color2 = color1 * 1.3; // Lighter version
    
    float3 gradientColor = lerp(color1, color2, gradientFactor);
    float alpha = 1.0;
    
    // Shape processing
    if (shape.type == PrimitiveType::PRIMITIVE_RECTANGLE) {
        if (shape.cornerRadius > 0.0) {
            float2 center = float2(0.5, 0.5);
            float2 size = float2(1.0, 1.0);
            
            float dist = distanceToRectangle(input.uv, center, size - shape.cornerRadius * 2.0);
            alpha = 1.0 - smoothStep(0.0, antiAliasingStrength, dist - shape.cornerRadius);
        }
    }
    
    output.color = float4(gradientColor + material.emission, alpha);
    return output;
}

/**
 * @brief Debug fragment shader
 * 
 * Renders primitives with debug information like UV coordinates,
 * world position, or other diagnostic data.
 */
[shader("fragment")]
FragmentOutput fragmentDebug(VertexOutput input) {
    FragmentOutput output;
    
    // Show UV coordinates as colors
    float3 debugColor = float3(input.uv, 0.5);
    
    // Checkerboard pattern for UV visualization
    float2 grid = floor(input.uv * 8.0);
    float checker = fmod(grid.x + grid.y, 2.0);
    debugColor *= (0.7 + 0.3 * checker);
    
    output.color = float4(debugColor, 1.0);
    return output;
}
