// default battlescape fragment shader

uniform int BUMPMAP;
uniform int ROUGHMAP;
uniform int SPECULARMAP;
uniform int DYNAMICLIGHTS;
uniform float GLOWSCALE;

/* diffuse texture */
uniform sampler2D SAMPLER0;
/* lightmap or specularmap*/
uniform sampler2D SAMPLER1;
/* deluxemap or roughnessmap*/
uniform sampler2D SAMPLER2;
/* normalmap */
uniform sampler2D SAMPLER3;
/* glowmap */
uniform sampler2D SAMPLER4;

const vec3 two = vec3(2.0);
const vec3 negHalf = vec3(-0.5);

#define R_DYNAMIC_LIGHTS #replace r_dynamic_lights
#if r_dynamic_lights
varying vec3 lightDirs[R_DYNAMIC_LIGHTS];
#endif

#include "light_fs.glsl"
#include "bump_fs.glsl"
#include "fog_fs.glsl"
#include "cook-torrance_fs.glsl"

/**
 * main
 */
void main(void){

	vec4 finalColor = vec4(0.0);


	/* use new dynamic lighing system, including 
	 * the Cook-Torrance specularity model with the Phong
	 * model as a default if the roughness map isn't enabled */
	if (DYNAMICLIGHTS > 0) {
		finalColor = IlluminateFragment();
	} else {
		/* use static lighting (ie. legacy rendering code) */
		vec2 offset = vec2(0.0);
		vec3 bump = vec3(1.0);

		/* lightmap contains pre-computed incoming light color */
		vec3 lightmap = texture2D(SAMPLER1, gl_TexCoord[1].st).rgb;

#if r_bumpmap
		if(BUMPMAP > 0){  // sample deluxemap and normalmap
			vec4 normalmap = texture2D(SAMPLER3, gl_TexCoord[0].st);
			normalmap.rgb = normalize(two * (normalmap.rgb + negHalf));

			/* deluxemap contains pre-computed incoming light vectors in object tangent space */
			vec3 deluxemap = texture2D(SAMPLER2, gl_TexCoord[1].st).rgb;
			deluxemap = normalize(two * (deluxemap + negHalf));

			// resolve parallax offset and bump mapping
			offset = BumpTexcoord(normalmap.a);
			bump = BumpFragment(deluxemap, normalmap.rgb);
		}
#endif

		// sample the diffuse texture, honoring the parallax offset
		vec4 diffuse = texture2D(SAMPLER0, gl_TexCoord[0].st + offset);

		// factor in bump mapping
		diffuse.rgb *= bump;

		// otherwise, add to lightmap
		finalColor = LightFragment(diffuse, lightmap);
	}

#if r_fog
	finalColor = FogFragment(finalColor);  // add fog
#endif

#if r_postprocess
	gl_FragData[0] = finalColor;
	if(GLOWSCALE > 0.0){
		vec4 glowcolor = texture2D(SAMPLER4, gl_TexCoord[0].st);
		gl_FragData[1].rgb = glowcolor.rgb * glowcolor.a * GLOWSCALE;
		gl_FragData[1].a = 1.0;
	} 
#else 
	gl_FragColor = finalColor;
#endif

// developer tools

#if r_debug_normals
	if(DYNAMICLIGHTS > 0) {
		gl_FragData[0] = (1.0 + dot(vec3(0.0, 0.0, 1.0), normalize(-lightDirs[0]))) * 0.5 * vec4(1.0);
		gl_FragData[1] = vec4(0.0);
	}
#endif

#if r_debug_tangents
	if(DYNAMICLIGHTS > 0) {
		gl_FragData[0].r = (1.0 + dot(vec3(1.0, 0.0, 0.0), normalize(-lightDirs[0]))) * 0.5;
		gl_FragData[0].g = (1.0 + dot(vec3(0.0, 1.0, 0.0), normalize(-lightDirs[0]))) * 0.5;
		gl_FragData[0].b = (1.0 + dot(vec3(0.0, 0.0, 1.0), normalize(-lightDirs[0]))) * 0.5;
		gl_FragData[0].a = 1.0;
		gl_FragData[1] = vec4(0.0);
	}
#endif


#if r_normalmap
	vec3 n.rgb = normalize(2.0 * (texture2D(SAMPLER3, gl_TexCoord[0].st).rgb - 0.5));
	gl_FragData[0] = (1.0 + dot(n, normalize(-lightDirs[0]))) * 0.5 * vec4(1.0);
	gl_FragData[1] = vec4(0.0);
#endif

#if r_lightmap
	gl_FragData[0].rgb = lightmap;
	gl_FragData[0].a = 1.0;
	gl_FragData[1] = vec4(0.0);
#endif

#if r_deluxemap
	if(BUMPMAP > 0){
		gl_FragData[0].rgb = deluxemap;
		gl_FragData[0].a = 1.0;
		gl_FragData[1] = vec4(0.0);
	}
#endif

}
