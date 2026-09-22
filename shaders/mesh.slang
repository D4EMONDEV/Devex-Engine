// Draws meshes with physically based materials into the high dynamic range scene color. Vertices,
// scene data and materials are read through buffer device addresses passed in push constants
// (vertex pulling), so the pipeline has no vertex input state.

import common;
import pbr;

[[vk::push_constant]]
ConstantBuffer<DrawData> draw;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    const Vertex vertex = draw.vertices[vertexIndex];
    const float4x4 world = vertexTransform(draw, vertexIndex);
    const float4 worldPosition = mul(world, float4(vertex.position, 1.0));

    // Normals transform with the cofactor matrix, which stays correct under non-uniform scales.
    // Its sign follows the determinant so that mirrored transforms keep outward normals.
    const float3x3 linear = float3x3(world);
    const float3x3 columns = transpose(linear);
    const float3 normal = cross(columns[1], columns[2]) * vertex.normal.x +
                          cross(columns[2], columns[0]) * vertex.normal.y +
                          cross(columns[0], columns[1]) * vertex.normal.z;
    const float determinant = dot(columns[0], cross(columns[1], columns[2]));

    VertexOutput output;
    output.position = mul(draw.scene->viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = determinant < 0.0 ? -normal : normal;
    output.tangent = float4(mul(linear, vertex.tangent.xyz), vertex.tangent.w * sign(determinant + 1e-12));
    output.uv = float2(vertex.u, vertex.v);
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    let scene = draw.scene;
    let material = scene->materials[draw.material];

    const float4 baseColor =
        material.baseColorFactor * sampleTexture(material.baseColorTexture, input.uv);
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
    float occlusion =
        lerp(1.0, sampleTexture(material.occlusionTexture, input.uv).r, material.occlusionStrength);
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
    const float3 emission =
        material.emissiveFactor * sampleTexture(material.emissiveTexture, input.uv).rgb;
    // A blended surface lets (1 - alpha) of what is behind it through; everything else covers it.
    const float alpha = material.alphaMode == alphaModeBlend ? saturate(baseColor.a) : 1.0;
    return float4(luminance + emission, alpha);
}
