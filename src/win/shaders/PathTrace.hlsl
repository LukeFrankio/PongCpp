/**
 * PathTrace.hlsl - Direct3D 12 Compute Shader for Path Tracing
 * 
 * Phase 5: GPU-accelerated path tracer for Pong scene
 * Ports the CPU path tracing logic from soft_renderer.cpp to HLSL
 */

// Output textures
RWTexture2D<float4> OutputTexture : register(u0);    // Current frame output
RWTexture2D<float4> AccumTexture : register(u1);     // Temporal accumulation

// Scene data (structured buffer)
struct SceneObject {
    float4 data0;  // type, center.xyz
    float4 data1;  // radius, material, padding
    float4 data2;  // box min (for boxes)
    float4 data3;  // box max (for boxes)
};
StructuredBuffer<SceneObject> SceneData : register(t0);

// Render parameters (constant buffer)
cbuffer RenderParams : register(b0) {
    uint g_width;
    uint g_height;
    uint g_raysPerPixel;
    uint g_maxBounces;
    
    float g_accumAlpha;      // Temporal accumulation factor
    float g_emissiveIntensity;
    float g_roughness;
    uint g_frameIndex;
    
    uint g_numSpheres;
    uint g_numBoxes;
    uint g_resetHistory;     // 1 = clear accumulation
    uint g_padding;
    
    float4 g_cameraOrigin;
    float4 g_cameraLookAt;
    float g_cameraFOV;
    float3 g_padding2;
};

// Fast XorShift RNG (matches CPU implementation)
uint xorshift(inout uint state) {
    uint x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

float rng1(inout uint seed) {
    return float(xorshift(seed) & 0xFFFFFF) * (1.0 / 16777216.0);
}

void rng2(inout uint seed, out float u, out float v) {
    uint r = xorshift(seed);
    u = float(r & 0xFFFF) * (1.0 / 65536.0);
    v = float(r >> 16) * (1.0 / 65536.0);
}

// Random direction in hemisphere (cosine-weighted)
float3 randomHemisphere(float3 normal, inout uint seed) {
    float u, v;
    rng2(seed, u, v);
    
    // Cosine-weighted hemisphere sampling
    float r = sqrt(u);
    float theta = 6.28318531 * v;
    float x = r * cos(theta);
    float z = r * sin(theta);
    float y = sqrt(max(0.0, 1.0 - u));
    
    // Create tangent space
    float3 tangent = abs(normal.y) < 0.999 
        ? normalize(cross(normal, float3(0, 1, 0)))
        : normalize(cross(normal, float3(1, 0, 0)));
    float3 bitangent = cross(tangent, normal);
    
    return normalize(tangent * x + normal * y + bitangent * z);
}

// Ray-sphere intersection
bool intersectSphere(float3 ro, float3 rd, float3 center, float radius, 
                     float tMax, out float t, out float3 normal) {
    float3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    
    if (discriminant < 0.0) return false;
    
    float sqrtDisc = sqrt(discriminant);
    float t0 = -b - sqrtDisc;
    float t1 = -b + sqrtDisc;
    
    if (t0 > 0.001 && t0 < tMax) {
        t = t0;
        normal = normalize((ro + rd * t) - center);
        return true;
    }
    
    if (t1 > 0.001 && t1 < tMax) {
        t = t1;
        normal = normalize((ro + rd * t) - center);
        return true;
    }
    
    return false;
}

// Ray-box intersection (AABB)
bool intersectBox(float3 ro, float3 rd, float3 boxMin, float3 boxMax,
                  float tMax, out float t, out float3 normal) {
    float3 invDir = 1.0 / (rd + 1e-8);
    float3 t0 = (boxMin - ro) * invDir;
    float3 t1 = (boxMax - ro) * invDir;
    
    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);
    
    float tNear = max(max(tmin.x, tmin.y), tmin.z);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);
    
    if (tNear > tFar || tFar < 0.001 || tNear > tMax) return false;
    
    t = tNear > 0.001 ? tNear : tFar;
    
    // Compute normal
    float3 hitPoint = ro + rd * t;
    float3 center = (boxMin + boxMax) * 0.5;
    float3 p = hitPoint - center;
    float3 d = (boxMax - boxMin) * 0.5;
    float bias = 1.001;
    
    normal = float3(
        float(abs(p.x) > d.x * bias),
        float(abs(p.y) > d.y * bias),
        float(abs(p.z) > d.z * bias)
    ) * sign(p);
    
    normal = normalize(normal);
    return true;
}

// Trace ray through scene
struct HitInfo {
    float t;
    float3 position;
    float3 normal;
    int material;  // 0=diffuse, 1=emissive, 2=metal
    bool hit;
};

HitInfo traceRay(float3 ro, float3 rd) {
    HitInfo best;
    best.t = 1e20;
    best.hit = false;
    best.material = 0;
    
    // Trace spheres
    for (uint i = 0; i < g_numSpheres; i++) {
        SceneObject obj = SceneData[i];
        float3 center = obj.data0.yzw;
        float radius = obj.data1.x;
        int material = int(obj.data1.y);
        
        float t;
        float3 normal;
        if (intersectSphere(ro, rd, center, radius, best.t, t, normal)) {
            best.t = t;
            best.position = ro + rd * t;
            best.normal = normal;
            best.material = material;
            best.hit = true;
        }
    }
    
    // Trace boxes (paddles)
    for (uint j = 0; j < g_numBoxes; j++) {
        SceneObject obj = SceneData[g_numSpheres + j];
        float3 boxMin = obj.data2.xyz;
        float3 boxMax = obj.data3.xyz;
        int material = int(obj.data1.y);
        
        float t;
        float3 normal;
        if (intersectBox(ro, rd, boxMin, boxMax, best.t, t, normal)) {
            best.t = t;
            best.position = ro + rd * t;
            best.normal = normal;
            best.material = material;
            best.hit = true;
        }
    }
    
    return best;
}

// Fresnel-Schlick approximation
float fresnel(float cosTheta, float F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Path tracing kernel
[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    uint x = DTid.x;
    uint y = DTid.y;
    
    if (x >= g_width || y >= g_height) return;
    
    // Initialize RNG with pixel + frame
    uint seed = (x * 1973 + y * 9277 + g_frameIndex * 26699) | 1;
    
    // Accumulate multiple samples per pixel
    float3 color = float3(0, 0, 0);
    
    for (uint sample = 0; sample < g_raysPerPixel; sample++) {
        // Generate camera ray (orthographic for 2D game view)
        float u = (float(x) + rng1(seed)) / float(g_width);
        float v = (float(y) + rng1(seed)) / float(g_height);
        
        // Map to [-4, 4] x [-3, 3] world space
        float3 ro = float3((u * 2.0 - 1.0) * 4.0, (v * 2.0 - 1.0) * 3.0, 10.0);
        float3 rd = float3(0, 0, -1);
        
        // Path tracing loop
        float3 throughput = float3(1, 1, 1);
        float3 radiance = float3(0, 0, 0);
        
        for (uint bounce = 0; bounce < g_maxBounces; bounce++) {
            HitInfo hit = traceRay(ro, rd);
            
            if (!hit.hit) {
                // Background gradient
                float t = (rd.y + 1.0) * 0.5;
                float3 bgBottom = float3(0.05, 0.05, 0.1);
                float3 bgTop = float3(0.5, 0.7, 1.0) * 0.3;
                radiance += throughput * lerp(bgBottom, bgTop, t);
                break;
            }
            
            // Emissive material
            if (hit.material == 1) {
                radiance += throughput * float3(1, 1, 1) * g_emissiveIntensity;
                break;
            }
            
            // Russian roulette termination
            float maxComponent = max(max(throughput.x, throughput.y), throughput.z);
            if (bounce > 2 && maxComponent < 0.1) {
                float rr = rng1(seed);
                if (rr > maxComponent) break;
                throughput /= maxComponent;
            }
            
            // Material evaluation
            if (hit.material == 2) {
                // Metal (roughness-based reflection)
                float3 reflected = reflect(rd, hit.normal);
                
                // Add roughness perturbation
                float3 fuzz = randomHemisphere(hit.normal, seed) * g_roughness;
                rd = normalize(reflected + fuzz);
                
                // Fresnel for metals
                float NdotV = max(dot(-rd, hit.normal), 0.0);
                float F = fresnel(NdotV, 0.7); // Metallic base reflectance
                throughput *= F * float3(0.9, 0.9, 0.95); // Slight blue tint
                
            } else {
                // Diffuse (Lambertian)
                rd = randomHemisphere(hit.normal, seed);
                
                // Lambert term (cosine falloff)
                float NdotL = max(dot(rd, hit.normal), 0.0);
                throughput *= float3(0.7, 0.7, 0.7) * NdotL;
            }
            
            // Offset ray origin to avoid self-intersection
            ro = hit.position + hit.normal * 0.002;
            
            // Early termination if throughput too low
            if (max(max(throughput.x, throughput.y), throughput.z) < 0.001) break;
        }
        
        color += radiance;
    }
    
    // Average samples
    color /= float(g_raysPerPixel);
    
    // Temporal accumulation
    float4 prevAccum = AccumTexture[uint2(x, y)];
    
    if (g_resetHistory == 1 || prevAccum.w < 0.5) {
        // First frame or reset - just store current
        AccumTexture[uint2(x, y)] = float4(color, 1.0);
        OutputTexture[uint2(x, y)] = float4(color, 1.0);
    } else {
        // Exponential moving average
        float3 accumulated = lerp(prevAccum.rgb, color, g_accumAlpha);
        AccumTexture[uint2(x, y)] = float4(accumulated, 1.0);
        OutputTexture[uint2(x, y)] = float4(accumulated, 1.0);
    }
}
