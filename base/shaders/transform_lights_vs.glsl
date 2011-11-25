/**
 * @file transform_lights_vs.glsl
 * @brief Transform lights to the tangent space. Requires light_vs.glsl and lerp_vs.glsl.
 */

in_qualifier vec4 TANGENT;

out_qualifier vec3 eyedir;

#define R_DYNAMIC_LIGHTS #replace r_dynamic_lights
#if r_dynamic_lights
out_qualifier vec4 lightDirs[R_DYNAMIC_LIGHTS];
out_qualifier vec4 lightParams[R_DYNAMIC_LIGHTS];
#endif

/**
 * @brief TransformLights
 */
void TransformLights(void) {
	/* Load the tangent.*/
	vec3 tangent = normalize(gl_NormalMatrix * Tangent.xyz);
	/* Compute the bitangent.*/
	vec3 bitangent = normalize(cross(normal, tangent)) * Tangent.w;

	/* Transform the eye direction into tangent space.*/
	vec3 v;
	v.x = dot(-point, tangent);
	v.y = dot(-point, bitangent);
	v.z = dot(-point, normal);

	eyedir = normalize(v);

	/* Transform relative light positions into tangent space.*/

	vec3 lpos;
#unroll r_dynamic_lights
	if (gl_LightSource[$].position.w != 0.0) {
		lpos = gl_LightSource[$].position.xyz - point;
	} else { /* directional light source at "infinite" distance */
		lpos = normalize(gl_LightSource[$].position.xyz);
	}
	lightDirs[$].x = dot(lpos, tangent);
	lightDirs[$].y = dot(lpos, bitangent);
	lightDirs[$].z = dot(lpos, normal);
	lightDirs[$].w = gl_LightSource[$].position.w;
	lightParams[$].rgb = gl_LightSource[$].diffuse.rgb;
	lightParams[$].a = gl_LightSource[$].quadraticAttenuation;
#endunroll
}
