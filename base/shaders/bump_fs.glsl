// bumpmap fragment shader

varying vec3 eyedir;

uniform float BUMP;
uniform float PARALLAX;
uniform float HARDNESS;
uniform float SPECULAR;

vec3 V;


/*
 * BumpTexcoord
 */
vec2 BumpTexcoord(in float height){

	V = normalize(eyedir);

  /* parallax is currently somewhat broken; it will be re-enabled for future releases */
	//return vec2(height * PARALLAX * 0.04 - 0.02) * V.xy;
  return vec2(0.0);
}


/*
 *BumpFragment
 */
vec3 BumpFragment(in vec3 lightVec, in vec3 normalVec){

	V = normalize(eyedir);
	vec3 L = vec3(normalize(lightVec).rgb);
	vec3 N = vec3(normalize(normalVec).rgb);
	N.xy *= BUMP;

	float diffuse = dot(N, L);

	float specular = HARDNESS * pow(max(-dot(V, reflect(L, N)), 0.0), 
									8.0 * SPECULAR);

	return vec3(diffuse + specular);
}
