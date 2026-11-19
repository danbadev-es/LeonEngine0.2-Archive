#!/usr/bin/env python3
"""
Mutation testing of the engine's HLSL shaders.

Breaks one line of a shader at a time (a wrong exponent, a dropped term, an inverted test) and runs
the GPU shader tests. A mutation the tests do not notice is a gap in the tests.

The tests compile the shaders from Engine/Shaders when they start, so nothing is rebuilt: each
mutation costs one run of the shader suites (they stop at the first failure).

    python Tools/run_shader_mutations.py                 every mutation
    python Tools/run_shader_mutations.py SHADOW BLOOM    only ids containing one of these
    python Tools/run_shader_mutations.py --list

Exit code 0 when every mutation is caught. Do not build, run tests or start the editor while this
runs: it edits files under Engine/Shaders (and restores them, also when interrupted).
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_EXE = ROOT_DIR / "out" / "build" / "dev-debug" / "bin" / "RendererTests.exe"
SHADER_DIR = "Engine/Shaders"
TEST_FILTER = "*Shader*,*golden*"

COMMON = "Common.hlsli"
PBR = "PBR_Common.hlsli"
LIT = "PBR_Lit.hlsl"
SHADOWS = "Shadows.hlsli"
BLOOM = "Bloom.hlsl"
TONE = "ToneMapping.hlsl"
FXAA = "FXAA.hlsl"
SSAO = "SSAO.hlsl"
SKY = "Skybox.hlsl"


def M(mutation_id, file, name, target, replacement):
    return {"id": mutation_id, "name": name, "edits": [(file, target, replacement)]}


MUTATIONS = [
    # --- Cook-Torrance (PBR_Common.hlsli) ---
    M("PBR_FRESNEL_EXPONENT", PBR, "Fresnel-Schlick: exponent 5 -> 4",
      "return F0 + (1.0 - F0) * pow(saturate(1.0 - InCosTheta), 5.0);",
      "return F0 + (1.0 - F0) * pow(saturate(1.0 - InCosTheta), 4.0);"),
    M("PBR_FRESNEL_GRAZING", PBR, "Fresnel-Schlick: grazing limit goes to 0 instead of 1",
      "return F0 + (1.0 - F0) * pow(saturate(1.0 - InCosTheta), 5.0);",
      "return F0 + (0.0 - F0) * pow(saturate(1.0 - InCosTheta), 5.0);"),
    M("PBR_GGX_DENOMINATOR", PBR, "GGX: denominator not squared",
      "const float denominator = kPi * f * f;",
      "const float denominator = kPi * f;"),
    M("PBR_GGX_ALPHA", PBR, "GGX: roughness inverted in alpha",
      "const float a = InRoughness * InRoughness;",
      "const float a = (1.0 - InRoughness) * (1.0 - InRoughness);"),
    M("PBR_SMITH_G1", PBR, "Smith: masking term dropped (G1 only)",
      "return GeometrySchlickGGX(max(dot(N, L), 0.0), InRoughness) * GeometrySchlickGGX(max(dot(N, V), 0.0), InRoughness);",
      "return GeometrySchlickGGX(max(dot(N, L), 0.0), InRoughness);"),
    M("PBR_METALLIC_INVERTED", PBR, "Energy conservation: metallic inverted in the diffuse weight",
      "const float3 diffuseWeight = (1.0 - fresnel) * (1.0 - InSurface.Metallic);",
      "const float3 diffuseWeight = (1.0 - fresnel) * InSurface.Metallic;"),
    M("PBR_NO_DIFFUSE", PBR, "Direct light: diffuse reflection dropped",
      "OutDiffuse += (diffuseWeight * InSurface.Albedo / kPi) * arriving;",
      "OutDiffuse += float3(0.0, 0.0, 0.0) * arriving;"),
    M("PBR_LAMBERT_PI", PBR, "Direct light: Lambert without the division by pi",
      "OutDiffuse += (diffuseWeight * InSurface.Albedo / kPi) * arriving;",
      "OutDiffuse += (diffuseWeight * InSurface.Albedo) * arriving;"),
    M("PBR_NDOTL", PBR, "Direct light: cosine of the incidence angle ignored",
      "const float NdotL = max(dot(InSurface.N, L), 0.0);",
      "const float NdotL = 1.0;"),
    M("PBR_ATTENUATION", PBR, "Point light: inverse-square falloff dropped",
      "return (window * window) / (InDistance * InDistance + 1.0);",
      "return window * window;"),
    M("PBR_FOG_START", PBR, "Fog: start distance ignored",
      "const float distance = max(length(InWorldPosition - u_ViewPosition.xyz) - u_FogParams.x, 0.0);",
      "const float distance = length(InWorldPosition - u_ViewPosition.xyz);"),
    M("PBR_FOG_HEIGHT", PBR, "Fog: height falloff ignored",
      "const float fog = min(1.0 - exp(-distance * density * heightFactor), u_FogParams.w);",
      "const float fog = min(1.0 - exp(-distance * density), u_FogParams.w);"),
    M("PBR_FOG_MAX", PBR, "Fog: maximum opacity ignored",
      "const float fog = min(1.0 - exp(-distance * density * heightFactor), u_FogParams.w);",
      "const float fog = 1.0 - exp(-distance * density * heightFactor);"),

    # --- Surface shader (PBR_Lit.hlsl) ---
    M("LIT_SPOT_CONE", LIT, "Spot light: cone test inverted",
      "if (theta <= outerCos)",
      "if (theta > outerCos)"),
    M("LIT_IBL_DIFFUSE", LIT, "IBL: diffuse irradiance zeroed",
      "diffuseIBL = t_Irradiance.SampleLevel(s_LinearClamp, N, 0.0).rgb * u_EnvSkyColor.a * albedo / kPi;",
      "diffuseIBL = float3(0.0, 0.0, 0.0);"),
    M("LIT_IBL_BRDF_LUT", LIT, "IBL: split-sum table replaced by a constant",
      "const float2 envBRDF = t_BRDFLUT.SampleLevel(s_LinearClamp, float2(NdotV, roughness), 0.0).rg;",
      "const float2 envBRDF = float2(0.5, 0.5);"),
    M("LIT_PLANAR_TOGGLE", LIT, "Planar reflections: per-draw switch inverted",
      "if (u_DrawFlags.w == 1 && u_PlanarFlags.x == 1) {",
      "if (u_DrawFlags.w != 1 && u_PlanarFlags.x == 1) {"),
    M("LIT_PLANAR_ALIGNMENT", LIT, "Planar reflections: surfaces not facing the plane also reflect",
      "OutWeight = saturate(1.0 - InRoughness * 1.1) * inFront * inBounds * aligned * onPlane;",
      "OutWeight = saturate(1.0 - InRoughness * 1.1) * inFront * inBounds * onPlane;"),
    M("LIT_PLANAR_ROUGHNESS", LIT, "Planar reflections: rough surfaces reflect like mirrors",
      "OutWeight = saturate(1.0 - InRoughness * 1.1) * inFront * inBounds * aligned * onPlane;",
      "OutWeight = inFront * inBounds * aligned * onPlane;"),
    M("LIT_NO_DIRECT", LIT, "Output: direct light dropped",
      "float3 color = ambient + direct + baked + emissive;",
      "float3 color = ambient + baked + emissive;"),
    M("LIT_EMISSIVE_ANGLE", LIT, "Emissive: multiplied by the view angle",
      "float3 color = ambient + direct + baked + emissive;",
      "float3 color = ambient + direct + baked + emissive * NdotV;"),
    M("LIT_EMISSIVE_MAP", LIT, "Emissive: texture ignored",
      "emissive *= t_Emissive.Sample(s_LinearWrap, uv).rgb;",
      "emissive *= 1.0;"),
    M("LIT_NORMAL_DECODE", LIT, "Normal map: wrong range decoding (* 1 - 1)",
      "t_Normal.Sample(s_LinearWrap, uv).rgb * 2.0 - 1.0;",
      "t_Normal.Sample(s_LinearWrap, uv).rgb * 1.0 - 1.0;"),
    M("LIT_NORMAL_SCALE", LIT, "Normal map: strength ignored",
      "tangentNormal.xy *= u_SurfaceParams.w;",
      "tangentNormal.xy *= 1.0;"),
    M("LIT_NORMAL_FORCED", LIT, "Normal map: sampled even when the material has none",
      "if (textureBits & MATERIAL_HAS_NORMAL) {",
      "if (true) {"),
    M("LIT_ALBEDO_COLOR", LIT, "Albedo: colour multiplier dropped",
      "const float3 albedo = u_AlbedoColor.rgb * albedoSample.rgb;",
      "const float3 albedo = albedoSample.rgb;"),
    M("LIT_METALLIC_MAP", LIT, "Metallic: texture ignored",
      "metallic *= t_Metallic.Sample(s_LinearWrap, uv).r;",
      "metallic *= 1.0;"),
    M("LIT_ROUGHNESS_MAP", LIT, "Roughness: texture ignored",
      "roughness *= t_Roughness.Sample(s_LinearWrap, uv).r;",
      "roughness *= 1.0;"),
    M("LIT_AO_STRENGTH", LIT, "Ambient occlusion: strength ignored",
      "ao *= lerp(1.0, t_AO.Sample(s_LinearWrap, uv).r, u_SurfaceParams2.x);",
      "ao *= t_AO.Sample(s_LinearWrap, uv).r;"),
    M("LIT_ALPHA_INVERTED", LIT, "Alpha test: comparison inverted",
      "if (u_MaterialFlags.y == 1 && alpha < u_SurfaceParams2.y)",
      "if (u_MaterialFlags.y == 1 && alpha > u_SurfaceParams2.y)"),
    M("LIT_ALPHA_ALWAYS", LIT, "Alpha test: discards everything when masked",
      "if (u_MaterialFlags.y == 1 && alpha < u_SurfaceParams2.y)",
      "if (u_MaterialFlags.y == 1)"),
    M("LIT_UV_OFFSET", LIT, "UV transform: offset dropped",
      "const float2 uv = InPixel.TexCoord * u_UVTransform.xy + u_UVTransform.zw;",
      "const float2 uv = InPixel.TexCoord * u_UVTransform.xy;"),
    M("LIT_UV_TILING", LIT, "UV transform: tiling dropped",
      "const float2 uv = InPixel.TexCoord * u_UVTransform.xy + u_UVTransform.zw;",
      "const float2 uv = InPixel.TexCoord + u_UVTransform.zw;"),
    {"id": "LIT_TANGENT_GRAM_SCHMIDT", "name": "Tangent space: tangent not made perpendicular to the normal",
     "edits": [(LIT, "T = SafeNormalize(T - N * dot(N, T), float3(1.0, 0.0, 0.0));", "T = T;"),
               (LIT, "T = SafeNormalize(T - geometricNormal * dot(geometricNormal, T), float3(1.0, 0.0, 0.0));",
                "T = T;")]},
    M("LIT_BITANGENT_HANDEDNESS", LIT, "Tangent space: handedness of the tangent frame ignored",
      "output.Bitangent = cross(N, T) * InVertex.Tangent.w * mirror;",
      "output.Bitangent = cross(N, T) * mirror;"),
    M("LIT_CASCADE_DEBUG_COLOUR", LIT, "Cascade debug view: first cascade drawn in another colour",
      "const float3 cascadeColors[4] = {float3(1.0, 0.15, 0.15),",
      "const float3 cascadeColors[4] = {float3(0.15, 0.15, 1.0),"),

    # --- Shadows (Shadows.hlsli) ---
    M("SHADOW_CASCADE_SPLIT", SHADOWS, "Cascades: first split comparison inverted",
      "if (viewDepth < u_CascadeSplits.x)",
      "if (viewDepth > u_CascadeSplits.x)"),
    M("SHADOW_CASCADE_DEFAULT", SHADOWS, "Cascades: pixels past every split use the first cascade",
      "int cascade = lastCascade;",
      "int cascade = 0;"),
    M("SHADOW_NORMAL_OFFSET", SHADOWS, "Bias: normal offset pushed into the surface",
      "const float3 offset = normal * (u_ShadowParams.z * slopeFactor);",
      "const float3 offset = -normal * (u_ShadowParams.z * slopeFactor);"),
    M("SHADOW_CONSTANT_BIAS", SHADOWS, "Bias: constant term dropped",
      "const float bias = (u_ShadowParams.x + u_ShadowParams.y * ShadowSlopeTan(normal, InLightDir)) * InBiasScale;",
      "const float bias = (u_ShadowParams.y * ShadowSlopeTan(normal, InLightDir)) * InBiasScale;"),
    M("SHADOW_SLOPE_BIAS", SHADOWS, "Bias: slope term dropped",
      "const float bias = (u_ShadowParams.x + u_ShadowParams.y * ShadowSlopeTan(normal, InLightDir)) * InBiasScale;",
      "const float bias = u_ShadowParams.x * InBiasScale;"),
    M("SHADOW_HARD_INVERTED", SHADOWS, "Hard shadows: comparison result inverted",
      "return 1.0 - ShadowTap(InCascade, InShadowPosition.xy, InDepth);",
      "return ShadowTap(InCascade, InShadowPosition.xy, InDepth);"),
    M("SHADOW_FILTER_DIVISOR", SHADOWS, "Filtered shadows: wrong normalisation of the taps",
      "return taps < 0.5 ? 0.0 : 1.0 - lit / taps;",
      "return taps < 0.5 ? 0.0 : 1.0 - lit / (taps * 3.0);"),
    M("SHADOW_PCF5_RADIUS", SHADOWS, "PCF 5x5: uses the 3x3 kernel",
      "const int radius = filterMode == 1 ? 1 : 2;",
      "const int radius = 1;"),
    M("SHADOW_POISSON_COLLAPSED", SHADOWS, "Poisson: every tap at the centre",
      "const float2 offset = RotateKernel(kPoissonDisk[i], rotation) * 1.75 * texelSize;",
      "const float2 offset = RotateKernel(kPoissonDisk[i], rotation) * 0.0 * texelSize;"),
    M("SHADOW_CASCADE_BLEND", SHADOWS, "Cascades: blend between neighbours zeroed",
      "shadow = lerp(shadow, nextShadow, blend);",
      "shadow = lerp(shadow, nextShadow, 0.0);"),
    M("SHADOW_SPOT_FORCED", SHADOWS, "Spot shadow: everything in shadow",
      "return SampleShadowMap(SpotShadowMapId(InLayer), u_SpotLightSpaceMatrices[InLayer], InReceiver, InLightDir, 1.5,",
      "return 1.0 + 0.0 * SampleShadowMap(SpotShadowMapId(InLayer), u_SpotLightSpaceMatrices[InLayer], InReceiver, InLightDir, 1.5,"),
    M("SHADOW_SPOT_LAYER", SHADOWS, "Spot shadow: every light reads the first light's shadows",
      "return SampleShadowMap(SpotShadowMapId(InLayer), u_SpotLightSpaceMatrices[InLayer], InReceiver, InLightDir, 1.5,",
      "return SampleShadowMap(SpotShadowMapId(0), u_SpotLightSpaceMatrices[0], InReceiver, InLightDir, 1.5,"),
    M("SHADOW_FAR_FADE", SHADOWS, "Cascades: fade past the last one goes to full shadow",
      "shadow = lerp(shadow, 0.0, saturate((viewDepth - farSplit) / 15.0));",
      "shadow = lerp(shadow, 1.0, saturate((viewDepth - farSplit) / 15.0));"),
    M("SHADOW_POINT_INVERTED", SHADOWS, "Point shadow: depth comparison inverted",
      "return InDepth > stored + InBias ? 1.0 : 0.0;",
      "return InDepth < stored + InBias ? 1.0 : 0.0;"),

    # --- Bloom ---
    M("BLOOM_SOFT_KNEE", BLOOM, "Bright pass: soft knee dropped",
      "soft = (soft * soft) / (4.0 * knee + 0.00001);",
      "soft = 0.0;"),
    M("BLOOM_THRESHOLD", BLOOM, "Bright pass: threshold ignored",
      "const float contribution = max(soft, luma - u_BloomParams.x) / max(luma, 0.0001);",
      "const float contribution = 1.0;"),
    M("BLOOM_DOWNSAMPLE_WEIGHTS", BLOOM, "Downsample: centre box weight 0.125 -> 0.5",
      "result = (d + e + i + j) * 0.125 +",
      "result = (d + e + i + j) * 0.5 +"),
    M("BLOOM_UPSAMPLE_WEIGHTS", BLOOM, "Upsample: centre tap weight 4 -> 1",
      "float3 result = Tap(uv, 0.0, 0.0) * 4.0;",
      "float3 result = Tap(uv, 0.0, 0.0) * 1.0;"),

    # --- Tone mapping ---
    M("TONE_ACES", TONE, "ACES: linear numerator term 0.03 -> 0.35",
      "return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));",
      "return saturate((x * (2.51 * x + 0.35)) / (x * (2.43 * x + 0.59) + 0.14));"),
    M("TONE_EXPOSURE", TONE, "Exposure divides instead of multiplying",
      "const float3 exposed = (sceneHDR + bloomHDR * u_ToneParams.y) * max(u_ToneParams.x, 0.0);",
      "const float3 exposed = (sceneHDR + bloomHDR * u_ToneParams.y) / max(u_ToneParams.x, 0.0001);"),
    M("TONE_GAMMA", TONE, "Gamma: exponent inverted",
      "encoded = pow(max(ldr, 0.0), 1.0 / max(u_ToneParams.z, 0.0001));",
      "encoded = pow(max(ldr, 0.0), max(u_ToneParams.z, 0.0001));"),
    M("TONE_SRGB", COMMON, "sRGB encoding: exponent inverted",
      "const float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;",
      "const float3 high = 1.055 * pow(color, 2.4) - 0.055;"),
    M("TONE_BLOOM_INTENSITY", TONE, "Bloom intensity ignored in the composite",
      "const float3 exposed = (sceneHDR + bloomHDR * u_ToneParams.y) * max(u_ToneParams.x, 0.0);",
      "const float3 exposed = (sceneHDR + bloomHDR) * max(u_ToneParams.x, 0.0);"),

    # --- FXAA ---
    M("FXAA_THRESHOLD", FXAA, "Edge threshold early-out disabled",
      "if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))",
      "if (false)"),
    M("FXAA_ORIENTATION", FXAA, "Edge orientation decision inverted",
      "const bool bHorizontal = edgeHorizontal >= edgeVertical;",
      "const bool bHorizontal = edgeHorizontal < edgeVertical;"),

    # --- Ambient occlusion ---
    M("SSAO_PLANE_TEST", SSAO, "Occlusion: points on the surface's own plane count as occluders",
      "if (dot(toOccluder, normal) <= planeBias)",
      "if (false)"),
    M("SSAO_KEEP_HIGHLIGHTS", SSAO, "Composite: occlusion also dims bright pixels",
      "return float4(hdr * lerp(ao, 1.0, smoothstep(0.2, 1.25, luminance)), 1.0);",
      "return float4(hdr * ao, 1.0);"),

    # --- Sky ---
    M("SKY_PANORAMA_SEAM", SKY, "Panorama: mip level chosen from raw derivatives (seam where it wraps)",
      "t_Panorama.SampleGrad(s_LinearWrap, uv, dx, dy)",
      "t_Panorama.Sample(s_LinearWrap, uv)"),
]


def run_tests(exe):
    command = [str(exe), f"-ts={TEST_FILTER}", "--abort-after=1"]
    try:
        process = subprocess.run(command, cwd=ROOT_DIR, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, errors="replace", timeout=600)
        return process.returncode == 0, process.stdout
    except Exception as error:  # a hang or a crash counts as a failed run
        return False, str(error)


def first_failed_case(output):
    case = "?"
    for line in output.splitlines():
        if "TEST CASE:" in line:
            case = line.split("TEST CASE:")[-1].strip()
        if "ERROR:" in line and "CHECK" in line or "REQUIRE" in line and "ERROR" in line:
            return case
    return case


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("ids", nargs="*", help="run only mutations whose id contains one of these")
    parser.add_argument("--exe", default=str(DEFAULT_EXE), help="RendererTests executable")
    parser.add_argument("--list", action="store_true", help="list the mutations and exit")
    args = parser.parse_args()

    mutations = [m for m in MUTATIONS if not args.ids or any(part.upper() in m["id"] for part in args.ids)]
    if args.list:
        for m in mutations:
            print(f"{m['id']:<28} {m['name']}")
        return 0

    exe = Path(args.exe)
    if not exe.exists():
        print(f"[ERROR] {exe} not found: build RendererTests first.")
        return 2

    files = sorted({edit[0] for m in MUTATIONS for edit in m["edits"]})
    originals = {}
    for name in files:
        originals[name] = (ROOT_DIR / SHADER_DIR / name).read_bytes().decode("utf-8")

    passed, output = run_tests(exe)
    if not passed:
        print("[ERROR] The shader tests fail before any mutation:")
        print(output[-3000:])
        return 2
    print(f"Baseline passes. Running {len(mutations)} mutations.\n", flush=True)

    results = []
    try:
        for m in mutations:
            texts = {}
            missing = None
            for name, target, replacement in m["edits"]:
                text = texts.get(name, originals[name])
                # Shaders may be checked out with either line ending.
                if target not in text and target.replace("\n", "\r\n") in text:
                    target = target.replace("\n", "\r\n")
                    replacement = replacement.replace("\n", "\r\n")
                if text.count(target) != 1:
                    missing = f"{name}: target found {text.count(target)} times: {target[:60]}"
                    break
                texts[name] = text.replace(target, replacement)
            if missing:
                print(f"[STALE]   {m['id']}: {missing}", flush=True)
                results.append((m, "STALE", missing))
                continue

            for name, text in texts.items():
                (ROOT_DIR / SHADER_DIR / name).write_bytes(text.encode("utf-8"))
            try:
                passed, output = run_tests(exe)
            finally:
                for name in texts:
                    (ROOT_DIR / SHADER_DIR / name).write_bytes(originals[name].encode("utf-8"))

            if "failed to compile" in output:
                print(f"[INVALID] {m['id']}: the mutated shader does not compile", flush=True)
                results.append((m, "INVALID", ""))
            elif passed:
                print(f"[ESCAPED] {m['id']}: {m['name']}", flush=True)
                results.append((m, "ESCAPED", ""))
            else:
                case = first_failed_case(output)
                print(f"[caught]  {m['id']}  <- {case}", flush=True)
                results.append((m, "CAUGHT", case))
    finally:
        for name, text in originals.items():
            (ROOT_DIR / SHADER_DIR / name).write_bytes(text.encode("utf-8"))

    caught = sum(1 for r in results if r[1] == "CAUGHT")
    print("\n" + "=" * 78)
    print(f"Caught {caught} of {len(results)}")
    for status in ("ESCAPED", "STALE", "INVALID"):
        for m, result, detail in results:
            if result == status:
                print(f"  {status:<8} {m['id']:<28} {m['name']} {detail}")
    print("=" * 78)
    return 0 if caught == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
