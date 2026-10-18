// Lumen: contrast-adaptive sharpening (after AMD FidelityFX CAS, MIT) on the luma plane.
// Runs on the GPU inside mpv's renderer, so hardware-decoded frames never leave video memory.
// Installed into <data>\cache\shaders by Lumen; strength comes from --glsl-shader-opts.

//!PARAM lumen_cas_sharpness
//!TYPE float
//!MINIMUM 0.0
//!MAXIMUM 1.0
0.5

//!HOOK LUMA
//!BIND HOOKED
//!DESC Lumen sharpen (CAS)

vec4 hook() {
    // a b c
    // d e f
    // g h i
    float a = HOOKED_texOff(vec2(-1.0, -1.0)).x;
    float b = HOOKED_texOff(vec2( 0.0, -1.0)).x;
    float c = HOOKED_texOff(vec2( 1.0, -1.0)).x;
    float d = HOOKED_texOff(vec2(-1.0,  0.0)).x;
    float e = HOOKED_tex(HOOKED_pos).x;
    float f = HOOKED_texOff(vec2( 1.0,  0.0)).x;
    float g = HOOKED_texOff(vec2(-1.0,  1.0)).x;
    float h = HOOKED_texOff(vec2( 0.0,  1.0)).x;
    float i = HOOKED_texOff(vec2( 1.0,  1.0)).x;

    // Soft min and max over the cross and the full 3x3 ring.
    float mn = min(min(min(d, e), min(f, b)), h);
    float mn2 = min(mn, min(min(a, c), min(g, i)));
    mn += mn2;
    float mx = max(max(max(d, e), max(f, b)), h);
    float mx2 = max(mx, max(max(a, c), max(g, i)));
    mx += mx2;

    // Less sharpening where the neighbourhood is already near black or white: no halos.
    float amp = clamp(min(mn, 2.0 - mx) / max(mx, 1e-5), 0.0, 1.0);
    amp = sqrt(amp);

    // Filter shape:  0 w 0 / w 1 w / 0 w 0
    float peak = -1.0 / mix(8.0, 5.0, lumen_cas_sharpness);
    float w = amp * peak;
    float result = (b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w);
    return vec4(clamp(result, 0.0, 1.0), 0.0, 0.0, 1.0);
}
