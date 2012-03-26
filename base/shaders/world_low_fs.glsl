/**
 * @file world_low_fs.glsl
 * @brief Low quality battlescape fragment shader.
 */

#if r_postprocess
	/*
	 * Indicates that gl_FragData is written to, not gl_FragColor.
	 * #extension needs to be placed before all non preprocessor code.
	 */
	#extension GL_ARB_draw_buffers : enable
#endif

uniform int BUMPMAP;
uniform float BUMP;

/** Diffuse texture.*/
uniform sampler2D SAMPLER_DIFFUSE;
/** Lightmap.*/
uniform sampler2D SAMPLER_LIGHTMAP;
/** Deluxemap.*/
uniform sampler2D SAMPLER_DELUXEMAP;
/** Normalmap.*/
uniform sampler2D SAMPLER_NORMALMAP;

#define R_DYNAMIC_LIGHTS #replace r_dynamic_lights
#if r_dynamic_lights
in_qualifier vec3 lightDirs[R_DYNAMIC_LIGHTS];
uniform vec4 LIGHTPARAMS[R_DYNAMIC_LIGHTS];
#endif

const vec3 two = vec3(2.0);
const vec3 negHalf = vec3(-0.5);

#include "light_fs.glsl"
#include "fog_fs.glsl"
#include "world_devtools_fs.glsl"
#include "write_fragment_fs.glsl"

in_qualifier vec4 blendColor;

/**
 * @brief main
 */
void main(void) {
	vec4 finalColor = vec4(0.0);
	vec3 light = vec3(0.0);
	/* These two should be declared in this scope for developer tools to work */
	vec3 deluxemap = vec3(0.0, 0.0, 1.0);
	vec4 normalmap = vec4(0.0, 0.0, 1.0, 0.5);

	/* lightmap contains pre-computed incoming light color */
	light = texture2D(SAMPLER_LIGHTMAP, gl_TexCoord[1].st).rgb;

#if r_bumpmap
	if (BUMPMAP > 0) {
		/* Sample deluxemap and normalmap.*/
		normalmap = texture2D(SAMPLER_NORMALMAP, gl_TexCoord[0].st);
		normalmap.rgb = normalize(two * (normalmap.rgb + negHalf));

		/* deluxemap contains pre-computed incoming light vectors in object tangent space */
		deluxemap = texture2D(SAMPLER_DELUXEMAP, gl_TexCoord[1].st).rgb;
		deluxemap = normalize(two * (deluxemap + negHalf));

		/* Bump mapping.*/
		light *= clamp(dot(deluxemap, vec3(normalmap.x * BUMP, normalmap.y * BUMP, normalmap.z)), 0.0, 1.0);
	}
#endif

	/* Sample the diffuse texture, honoring the parallax offset.*/
	vec4 diffuse = texture2D(SAMPLER_DIFFUSE, gl_TexCoord[0].st);

	/* Add dynamic lights, if any */
	light = clamp(light + LightFragment(normalmap.rgb), 0.0, 2.0);

	finalColor.rgb = diffuse.rgb * light;
	finalColor.a = diffuse.a;

	finalColor *= blendColor;

#if r_fog
	/* Add fog.*/
	finalColor = FogFragment(finalColor);
#endif

	/* Developer tools, if enabled */
	finalColor = ApplyDeveloperTools(finalColor, normalmap.rgb, light, deluxemap);

	writeFragment(finalColor);
}
