// The passes that draw the meshes of the scene with their standard materials: the shading, the
// prepass, the shadows and the picking. Shared by the draws of one instance at a time (mesh.slang,
// prepass.slang, shadow.slang, pick.slang) and the draws of the instances the GPU culled
// (instanced.slang), which read what an instance needs from the buffer of the frame.

module geometry;

import common;
import pbr;

// An instance the GPU culls and draws, as the buffer of the frame holds it.
public struct GpuInstance
{
    public float4x4 world;
    // Where it stood on the previous frame, for the motion of its pixels.
    public float4x4 previousWorld;
    // The box of its mesh, in the space of the mesh, which the culling turns into the world. Each
    // vector stays within 16 bytes, as the layout of buffers wants it without scalar layouts.
    public float3 boundsMin;
    public uint objectId;
    public float3 boundsMax;
    // Its batch: the submesh, the material and the sides drawn.
    public uint batch;
    public Vertex* vertices;
    // What the culling looks for it in: 1 in the views of the camera, 2 in those of shadows.
    public uint flags;
    public uint padding;
};

// What the draws of the instances of a batch read: the instances of the frame, and the slots the
// culling filled for a view, each naming an instance. The material is that of the batch, the same
// for every fragment of the draw, which keeps its reads uniform.
public struct InstancedData
{
    public SceneData* scene;
    public GpuInstance* instances;
    public uint* visible;
    public uint cascade;
    public uint material;
};

// The draw of one instance of a batch, as a draw of its own would give it.
public DrawData instanceDraw(InstancedData data, uint slot, out GpuInstance instance)
{
    instance = data.instances[data.visible[slot]];
    DrawData draw;
    draw.scene = data.scene;
    draw.vertices = instance.vertices;
    draw.world = instance.world;
    draw.material = data.material;
    draw.cascade = data.cascade;
    draw.objectId = instance.objectId;
    // Its skin and bones are left unset: never read without bones, and a null address would need
    // 64-bit integers in SPIR-V.
    draw.skinned = 0;
    draw.instanceParameters = 0xFFFFFFFFu;
    draw.padding0 = 0;
    draw.padding1 = 0;
    draw.padding2 = 0;
    return draw;
}

// Cut-out materials keep only what is opaque, in every pass.
public void discardCutout(SceneData* scene, uint materialIndex, float2 uv)
{
    let material = scene->materials[materialIndex];
    if (material.alphaMode == alphaModeMask)
    {
        const float alpha = material.baseColorFactor.a * sampleTexture(material.baseColorTexture, uv).a;
        if (alpha < material.alphaCutoff)
        {
            discard;
        }
    }
}

// The shading of a surface.

public struct SurfaceVertex
{
    public float4 position : SV_Position;
    public float3 worldPosition : POSITION;
    public float3 normal : NORMAL;
    public float4 tangent : TANGENT;
    public float2 uv : TEXCOORD0;
};

public SurfaceVertex surfaceVertex(DrawData draw, uint vertexIndex)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4x4 world = vertexTransform(draw, vertexIndex);
    const float4 worldPosition = mul(world, float4(vertex.position, 1.0));

    SurfaceVertex output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    // Normals transform with the cofactor matrix, which stays correct under non-uniform scales.
    output.normal = transformNormal(world, vertex.normal);
    output.tangent = float4(mul(float3x3(world), vertex.tangent.xyz), vertex.tangent.w * handedness(world));
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

public float4 surfaceFragment(SceneData* scene, uint materialIndex, SurfaceVertex input, bool frontFacing)
{
    let material = scene->materials[materialIndex];

    const float4 baseColor = material.baseColorFactor * sampleTexture(material.baseColorTexture, input.uv);
    if (material.alphaMode == alphaModeMask && baseColor.a < material.alphaCutoff)
    {
        discard;
    }

    float3 normal = normalize(input.normal);
    float3 tangent = input.tangent.xyz - normal * dot(normal, input.tangent.xyz);
    // Back faces of double-sided materials are lit as seen from their side.
    if (!frontFacing)
    {
        normal = -normal;
    }

    // Tangent-space normal maps store X and Y; Z is reconstructed.
    const float2 encoded = sampleTexture(material.normalTexture, input.uv).rg * 2.0 - 1.0;
    const float2 tilt = encoded * material.normalScale;
    const float lengthSquared = dot(tangent, tangent);
    if (lengthSquared > 1e-10)
    {
        tangent *= rsqrt(lengthSquared);
        const float3 bitangent = cross(normal, tangent) * (input.tangent.w < 0.0 ? -1.0 : 1.0);
        const float z = sqrt(saturate(1.0 - dot(encoded, encoded)));
        normal = normalize(tangent * tilt.x + bitangent * tilt.y + normal * z);
    }

    const float4 metallicRoughness = sampleTexture(material.metallicRoughnessTexture, input.uv);
    const float metallic = saturate(material.metallicFactor * metallicRoughness.b);
    // Very low roughness makes highlights too small to sample: clamp it.
    const float roughness = clamp(material.roughnessFactor * metallicRoughness.g, 0.045, 1.0);
    float occlusion = lerp(1.0, sampleTexture(material.occlusionTexture, input.uv).r, material.occlusionStrength);
    // What the screen measured of the corners the surface sits in, which the pass before drew.
    const float2 screenUv = input.position.xy / float2(scene->viewportWidth, scene->viewportHeight);
    occlusion *= ambientOcclusionTexture.Sample(clampSampler, screenUv).r;

    const float3 toCamera = scene->cameraPosition - input.worldPosition;
    const float viewDistance = -mul(scene->view, float4(input.worldPosition, 1.0)).z;

    Surface surface;
    surface.position = input.worldPosition;
    surface.normal = normal;
    surface.view = normalize(toCamera);
    surface.diffuseColor = baseColor.rgb * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04), baseColor.rgb, metallic);
    surface.roughness = roughness;
    surface.alpha = roughness * roughness;
    surface.occlusion = occlusion;
    surface.viewDistance = viewDistance;
    surface.fragmentCoordinate = input.position.xy;

    const float3 luminance = shadeSurface(scene, surface) * scene->exposure;
    // Emission is relative to the exposure: an emissive color of 1 shows as white-ish on screen,
    // whatever the lighting.
    const float3 emission = material.emissiveFactor * sampleTexture(material.emissiveTexture, input.uv).rgb;
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    // Hazed by the air and veiled by the fog between it and the camera.
    return float4(applyAtmosphere(scene, luminance + emission, surface.position), alpha);
}

// The prepass: the depth, the motion of the pixels and the normals.

public struct DepthVertex
{
    public float4 position : SV_Position;
    // Clip positions without the jitter, whose difference is the motion of the pixel.
    public float4 current : TEXCOORD0;
    public float4 previous : TEXCOORD1;
    public float3 normal : NORMAL;
    public float2 uv : TEXCOORD2;
};

// Where the vertex stood on the previous frame: the same blend of bones, posed as they were.
float4x4 previousVertexTransform(DrawData draw, float4x4 previousWorld, float4x4* previousBones, uint vertexIndex)
{
    if (draw.skinned == 0)
    {
        return previousWorld;
    }
    const VertexSkin skin = draw.skin[vertexIndex];
    float4x4 blended = previousBones[skin.joints.x] * skin.weights.x;
    blended += previousBones[skin.joints.y] * skin.weights.y;
    blended += previousBones[skin.joints.z] * skin.weights.z;
    blended += previousBones[skin.joints.w] * skin.weights.w;
    const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
    return total > 0.0001 ? blended : previousWorld;
}

public DepthVertex depthVertex(DrawData draw, float4x4 previousWorld, float4x4* previousBones, uint vertexIndex)
{
    return movedDepthVertex(draw, previousVertexTransform(draw, previousWorld, previousBones, vertexIndex), vertexIndex);
}

// The same, given where the vertex stood on the previous frame.
public DepthVertex movedDepthVertex(DrawData draw, float4x4 previousTransform, uint vertexIndex)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4x4 world = vertexTransform(draw, vertexIndex);
    const float4 worldPosition = mul(world, float4(vertex.position, 1.0));
    const float4 previousWorldPosition = mul(previousTransform, float4(vertex.position, 1.0));

    DepthVertex output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    output.current = mul(draw.scene->unjitteredViewProjection, worldPosition);
    output.previous = mul(draw.scene->previousViewProjection, previousWorldPosition);
    // Normals transform with the cofactor matrix, as they do when the surface is shaded.
    output.normal = transformNormal(world, vertex.normal);
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

public struct DepthTargets
{
    // Where the pixel was on the previous frame, as a distance in texture coordinates.
    public float2 velocity : SV_Target0;
    // The normal of the surface, folded into two numbers.
    public float2 normal : SV_Target1;
};

public DepthTargets depthFragment(SceneData* scene, uint materialIndex, DepthVertex input, bool frontFacing)
{
    discardCutout(scene, materialIndex, input.uv);
    const float2 current = input.current.xy / input.current.w;
    const float2 previous = input.previous.xy / input.previous.w;
    const float3 normal = frontFacing ? input.normal : -input.normal;

    DepthTargets output;
    // Clip space spans two units where texture coordinates span one.
    output.velocity = (current - previous) * 0.5;
    output.normal = encodeNormal(normalize(normal));
    return output;
}

// The shadows, and the picking: positions in a view, and what the fragments need.

public struct ShadowVertex
{
    public float4 position : SV_Position;
    public float2 uv : TEXCOORD0;
    // The object picked; unused by the shadows.
    public nointerpolation uint objectId : OBJECT;
};

public ShadowVertex viewVertex(DrawData draw, float4x4 viewProjection, uint vertexIndex)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    ShadowVertex output;
    output.position = mul(viewProjection, mul(vertexTransform(draw, vertexIndex), float4(vertex.position, 1.0)));
    output.uv = float2(vertex.u, vertex.v);
    output.objectId = draw.objectId;
    return output;
}
