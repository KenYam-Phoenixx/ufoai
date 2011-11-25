/**
 * @file world_vs.glsl
 * @brief Default battlescape vertex shader.
 */

uniform float OFFSET;
uniform int BUMPMAP;
uniform int ANIMATE;
uniform int IS_A_MODEL;

/* from includes:
varying vec3 point;
varying vec3 normal;

attribute vec4 TANGENT;

attribute vec4 NEXT_FRAME_VERTS;
attribute vec4 NEXT_FRAME_NORMALS;
attribute vec4 NEXT_FRAME_TANGENTS;
uniform float TIME;
varying vec4 Tangent;

varying vec3 eyedir;
varying vec3 lightDirs[];
varying vec3 staticLightDir;

varying float fog;
*/

#include "lerp_vs.glsl"
#include "light_vs.glsl"
#include "transform_lights_vs.glsl"
#include "fog_vs.glsl"

/**
 * @brief Main.
 */
void main(void) {
	if (ANIMATE > 0) {
		lerpVertex();
	} else {
		Vertex = gl_Vertex;
		Normal = gl_Normal;
		Tangent = TANGENTS; /** @todo what if tangents are disabled? */
	}

	/* MVP transform into clip space.*/
	/*gl_Position = ftransform();*/
	gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * vec4(Vertex);


	/* Pass texcoords through.*/
	gl_TexCoord[0] = gl_MultiTexCoord0 + OFFSET;
	gl_TexCoord[1] = gl_MultiTexCoord1 + OFFSET;

	LightVertex();

	TransformLights();

#if r_fog
	FogVertex();
#endif
}
