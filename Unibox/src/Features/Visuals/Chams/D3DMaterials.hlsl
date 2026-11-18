#ifdef VERTEX_SHADER
float4 g_ViewProjection[4] : register(c0);
float4 g_Decode : register(c4);
float4 g_Eye : register(c5);
float4 g_Palette[159] : register(c8);

struct VertexOutput
{
	float4 position : POSITION;
	float3 normal : TEXCOORD0;
	float3 eye : TEXCOORD1;
	float3 model : TEXCOORD2;
	float3 local : TEXCOORD3;
};

float3 DecodeNormal(float4 packed)
{
	if (g_Decode.w == 0)
		return packed.xyz;
	float2 zSigns = (packed.xy - 128.0) < 0;
	float2 magnitude = abs(packed.xy - 128.0) - zSigns;
	float2 xySigns = (magnitude - 64.0) < 0;
	float2 xy = (abs(magnitude - 64.0) - xySigns) / 63.0;
	float3 normal = normalize(float3(xy, 1.0 - xy.x - xy.y));
	normal.xy *= 1.0 - 2.0 * xySigns;
	normal.z *= 1.0 - 2.0 * zSigns.x;
	return normal;
}

VertexOutput TransformVertex(float3 position, float4 packedNormal, float4 row0, float4 row1, float4 row2)
{
	float4 local = float4(position, 1.0);
	float4 world = float4(dot(row0, local), dot(row1, local), dot(row2, local), 1.0);
	float3 normal = DecodeNormal(packedNormal);
	VertexOutput output;
	output.position = float4(dot(g_ViewProjection[0], world), dot(g_ViewProjection[1], world),
		dot(g_ViewProjection[2], world), dot(g_ViewProjection[3], world));
	output.normal = float3(dot(row0.xyz, normal), dot(row1.xyz, normal), dot(row2.xyz, normal));
	output.eye = g_Eye.xyz - world.xyz;
	output.model = position;
	output.local = normal;
	return output;
}

VertexOutput Rigid(float3 position : POSITION, float4 normal : NORMAL)
{
	return TransformVertex(position, normal, g_Palette[0], g_Palette[1], g_Palette[2]);
}

VertexOutput Skinned(float3 position : POSITION, float4 normal : NORMAL, float2 weights : BLENDWEIGHT, float4 indices : BLENDINDICES)
{
	int3 slots = clamp(D3DCOLORtoUBYTE4(indices).xyz, 0, 52) * 3;
	float2 decoded = weights * g_Decode.y + g_Decode.z;
	float3 blend = float3(decoded, 1.0 - decoded.x - decoded.y);
	float4 row0 = g_Palette[slots.x] * blend.x + g_Palette[slots.y] * blend.y + g_Palette[slots.z] * blend.z;
	float4 row1 = g_Palette[slots.x + 1] * blend.x + g_Palette[slots.y + 1] * blend.y + g_Palette[slots.z + 1] * blend.z;
	float4 row2 = g_Palette[slots.x + 2] * blend.x + g_Palette[slots.y + 2] * blend.y + g_Palette[slots.z + 2] * blend.z;
	return TransformVertex(position, normal, row0, row1, row2);
}
#else
float4 g_Primary : register(c0);
float4 g_Secondary : register(c1);
float4 g_Surface : register(c2);
float4 g_Animation : register(c3);

float Hash(float3 p)
{
	p = frac(p * float3(0.1031, 0.11369, 0.13787));
	p += dot(p, p.yzx + 19.19);
	return frac((p.x + p.y) * p.z);
}

float Noise(float3 p)
{
	float3 cell = floor(p);
	float3 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = lerp(Hash(cell), Hash(cell + float3(1, 0, 0)), f.x);
	float b = lerp(Hash(cell + float3(0, 1, 0)), Hash(cell + float3(1, 1, 0)), f.x);
	float c = lerp(Hash(cell + float3(0, 0, 1)), Hash(cell + float3(1, 0, 1)), f.x);
	float d = lerp(Hash(cell + float3(0, 1, 1)), Hash(cell + float3(1, 1, 1)), f.x);
	return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}

float3 Film(float phase)
{
	return 0.5 + 0.5 * cos(6.2831853 * (phase + float3(0.0, 0.333333, 0.666667)));
}

float3 StudioSky(float3 reflected, float roughness)
{
	float sky = smoothstep(-0.2, 0.8, reflected.z);
	float3 environment = lerp(float3(0.07, 0.09, 0.13), float3(0.48, 0.65, 0.9), sky);
	float horizon = exp2(-abs(reflected.z) * lerp(18.0, 3.0, roughness));
	float panel = pow(saturate(dot(reflected, normalize(float3(-0.4, 0.3, 0.85)))), lerp(200.0, 3.0, roughness));
	return environment + horizon * float3(0.5, 0.42, 0.32) + panel * 2.0;
}

float HexEdge(float2 p)
{
	float2 cell = float2(1.7320508, 1.0);
	float2 a = frac(p / cell) * cell - cell * 0.5;
	float2 b = frac((p - cell * 0.5) / cell) * cell - cell * 0.5;
	float2 q = dot(a, a) < dot(b, b) ? a : b;
	q = abs(q);
	return 0.5 - max(q.y, dot(q, float2(0.8660254, 0.5)));
}

float3 Hue(float h)
{
	return saturate(abs(frac(h + float3(0.0, 0.666667, 0.333333)) * 6.0 - 3.0) - 1.0);
}

float Fbm(float3 p)
{
	float sum = Noise(p) * 0.5;
	sum += Noise(p * 2.03 + 17.1) * 0.25;
	sum += Noise(p * 4.07 + 31.7) * 0.125;
	return sum / 0.875;
}

float2 Hash2(float2 p)
{
	float3 q = frac(p.xyx * float3(0.1031, 0.1030, 0.0973));
	q += dot(q, q.yzx + 33.33);
	return frac((q.xx + q.yz) * q.zy);
}

float2 Voronoi(float2 x, float time)
{
	float2 cell = floor(x);
	float2 f = frac(x);
	float d1 = 8.0;
	float d2 = 8.0;
	[unroll]
	for (int j = -1; j <= 1; ++j)
	{
		[unroll]
		for (int i = -1; i <= 1; ++i)
		{
			float2 g = float2(i, j);
			float2 o = 0.5 + 0.45 * sin(time + 6.2831853 * Hash2(cell + g));
			float2 r = g + o - f;
			float d = dot(r, r);
			d2 = d < d1 ? d1 : min(d2, d);
			d1 = min(d1, d);
		}
	}
	d1 = sqrt(d1);
	return float2(d1, sqrt(d2) - d1);
}

float2 Planar(float3 q, float3 local)
{
	float3 a = abs(local);
	return a.z >= a.x && a.z >= a.y ? q.xy : (a.x >= a.y ? q.yz : q.xz);
}

float SkyLight(float3 n, float facing)
{
	float hemisphere = lerp(0.35, 1.0, n.z * 0.5 + 0.5);
	return saturate(hemisphere * 0.75 + facing * 0.25);
}

float3 Aero(float3 body, float3 glow, float rim)
{
	float inner = lerp(0.15, 1.0, pow(rim, 1.6));
	float outline = smoothstep(0.8, 0.9, rim);
	return body * inner + glow + outline;
}

float4 Material(float3 worldNormal : TEXCOORD0, float3 eye : TEXCOORD1, float3 model : TEXCOORD2, float3 localNormal : TEXCOORD3) : COLOR0
{
	float3 n = normalize(worldNormal);
	float3 v = normalize(eye);
	float facing = saturate(abs(dot(n, v)));
	float roughness = g_Surface.x;
	float reflectivity = g_Surface.y;
	float intensity = g_Surface.z;
	float rim = pow(1.0 - facing, g_Surface.w);
	float3 p = model * (0.06 * g_Animation.y);
	float3 m = model * g_Animation.y;
	float3 ln = normalize(localNormal);
	float edge = 1.0 - facing;
	float time = g_Animation.x;
	float3 primary = g_Primary.rgb;
	float3 secondary = lerp(primary, g_Secondary.rgb, g_Secondary.a);
	float3 reflected = reflect(-v, n);
	float3 sky = StudioSky(reflected, roughness);
	float light = 0.3 + 0.45 * saturate(dot(n, normalize(float3(-0.4, -0.3, 0.85)))) + 0.25 * facing;
	float3 halfVector = normalize(v + normalize(float3(-0.4, -0.3, 0.85)));
	float specular = pow(saturate(dot(n, halfVector)), lerp(180.0, 4.0, roughness));
	float fresnel = reflectivity + (1.0 - reflectivity) * pow(1.0 - facing, 5.0);
	float3 color = primary;
	float alpha = g_Primary.a;
#if EFFECT == 1
	color = primary;
#elif EFFECT == 2
	color = primary * light + secondary * rim * reflectivity * 0.3 + specular * reflectivity * 0.35;
#elif EFFECT == 3
	color = primary * (0.12 + light * roughness * 0.5) + sky * lerp(primary, secondary, rim) * reflectivity + specular * (1.0 - roughness);
#elif EFFECT == 4
	float3 pearl = lerp(primary, secondary, 0.5 + 0.5 * sin(facing * 9.0 + Noise(p * 0.5) * 2.0 + time * 0.2));
	color = pearl * light + sky * fresnel * 0.45 + specular * 0.7;
#elif EFFECT == 5
	color = lerp(primary * 0.2, secondary, rim) + sky * fresnel + specular;
	alpha *= saturate(0.12 + rim * 0.75 + reflectivity * 0.18);
#elif EFFECT == 6
	float scan = pow(0.5 + 0.5 * sin(p.z * 34.0 - time * 7.0), 6.0);
	float flicker = 0.88 + 0.12 * sin(time * 17.0 + floor(p.z * 4.0));
	color = (lerp(primary * 0.25, secondary, scan) + secondary * rim * 1.8) * flicker;
	alpha *= saturate(0.25 + scan * 0.5 + rim * 0.5);
#elif EFFECT == 7
	float3 film = Film(facing * 1.7 + Noise(p * 0.3) * 0.25 + time * 0.12);
	color = lerp(primary, secondary, film) * light + film * sky * fresnel + specular * 0.5;
#elif EFFECT == 8
	color = sky * lerp(primary, secondary, rim) * (0.5 + reflectivity * 1.5) + primary * light * roughness * 0.2 + specular;
#elif EFFECT == 9
	float gradient = 0.5 + 0.5 * sin(p.z * 0.8 + p.x * 0.2 - time * 0.45);
	color = lerp(primary, secondary, gradient) * light + specular * reflectivity + secondary * rim * 0.2;
#elif EFFECT == 10
	color = lerp(primary, secondary, rim) * (0.6 + reflectivity * 0.4);
#elif EFFECT == 11
	float3 facet = normalize(floor(n * lerp(22.0, 5.0, roughness) + 0.5));
	float facetLight = saturate(dot(facet, normalize(float3(-0.4, -0.3, 0.85))));
	color = lerp(primary * 0.15, secondary, facetLight) + StudioSky(reflect(-v, facet), roughness * 0.4) * fresnel + rim * secondary;
	alpha *= 0.55 + 0.45 * rim;
#elif EFFECT == 12
	float pulse = 0.8 + 0.2 * sin(time * 2.0 + p.z * 0.2);
	color = lerp(primary, secondary, rim) * (1.2 + reflectivity) * pulse + secondary * rim;
#elif EFFECT == 13
	float field = sin(p.x * 2.4 + time) + sin(p.z * 2.0 - time * 1.3) + sin(length(p.xy) * 2.0 - time * 1.6);
	float energy = pow(saturate(1.0 - abs(sin(field + Noise(p + time * 0.2)))), 4.0);
	color = lerp(primary * 0.25, secondary * 2.5, energy) + secondary * rim * reflectivity;
#elif EFFECT == 14
	float cloud = Noise(p * 0.65 + float3(time * 0.07, 0, time * 0.03));
	cloud = saturate(cloud * 0.7 + Noise(p * 1.8 + cloud * 3.0 - time * 0.1) * 0.5);
	float stars = pow(Hash(floor(p * 13.0)), 70.0) * pow(saturate(Noise(p * 13.0)), 3.0);
	color = lerp(primary * 0.08, secondary * 1.8, cloud * cloud) + stars * 8.0 + rim * primary * reflectivity;
#elif EFFECT == 15
	float2 uv = Planar(p, ln);
	float2 weaveCell = floor(uv * 5.0);
	float orientation = fmod(abs(weaveCell.x + weaveCell.y), 2.0);
	float fiber = pow(abs(sin(lerp(uv.x, uv.y, orientation) * 120.0)), 4.0);
	float weave = lerp(0.18, 0.6, orientation) + fiber * 0.2;
	color = lerp(primary, secondary, orientation * 0.35) * weave * light + sky * fresnel * (0.2 + fiber * 0.5) + specular * reflectivity;
#elif EFFECT == 16
	float grooves = Noise(float3(p.x * 1.2, p.y * 95.0, p.z * 95.0));
	float anisotropic = pow(saturate(1.0 - abs(dot(reflected, normalize(float3(1.0, 0.15, 0.1))))), lerp(70.0, 3.0, roughness));
	color = lerp(primary, secondary, grooves * 0.25) * (light * 0.3 + grooves * 0.25) + sky * reflectivity * 0.6 + anisotropic * reflectivity;
#elif EFFECT == 17
	float hex = HexEdge(Planar(p, ln) * 2.5);
	float width = max(fwidth(hex), 0.008);
	float grid = 1.0 - smoothstep(0.025 - width, 0.025 + width, hex);
	float wave = 0.5 + 0.5 * sin(p.z * 1.8 + p.x - time * 2.0);
	color = primary * light * 0.25 + secondary * grid * (0.8 + wave * 1.5) + sky * fresnel * 0.2 + rim * secondary * 0.3;
#elif EFFECT == 18
	float3 cell = floor(p * 4.0);
	float tick = floor(time * 5.0);
	float bit = step(0.64, Hash(cell + float3(0, tick, 0)));
	float2 gridPosition = frac(Planar(p, ln) * 4.0);
	float grid = step(0.12, gridPosition.x) * step(0.12, gridPosition.y);
	float trail = pow(frac(p.z * 0.18 + Hash(float3(cell.xy, 0)) - time * 0.3), 8.0);
	color = primary * 0.12 + secondary * grid * (bit * 0.9 + trail * 2.2) + secondary * rim * reflectivity;
#elif EFFECT == 19
	float wobble = 0.75 + 0.22 * sin(time * 0.4947);
	float3 flow = m * (0.09 / wobble) - float3(0.0, 0.0, time * 0.6);
	float heat = saturate(Noise(flow) * 0.65 + Noise(flow * 2.3 + 17.0) * 0.35);
	float pulse = 0.78 + 0.17 * sin(time * 1.03) + 0.05 * sin(time * 2.73);
	float3 molten = primary * lerp(1.0, heat * 2.0, pulse) + secondary * pow(heat, 6.0) * 1.5;
	color = lerp(molten * SkyLight(n, facing), molten, saturate(facing * 1.25 - 0.25)) + secondary * rim * reflectivity * 0.5;
#elif EFFECT == 20
	float bubble = saturate(dot(n, normalize(v + float3(0.0, 0.0, 0.5)))) * 3.0;
	float3 bubbleTop = secondary.gbr;
	float3 bubbleColor = bubble < 1.0 ? primary * bubble : (bubble < 2.0 ? lerp(primary, secondary, bubble - 1.0) : lerp(secondary, bubbleTop, bubble - 2.0));
	color = bubbleColor * 2.0 + specular * reflectivity * 0.5;
#elif EFFECT == 21
	float neonEdge = edge * edge * edge;
	float3 neon = Hue(edge * 0.6 + (m.z + m.x * 0.5) * 0.015625 - time * 0.2);
	color = primary * (0.2 + edge * 0.3) + neon * (0.1 + neonEdge * 1.5) + neonEdge * neonEdge * 0.4;
#elif EFFECT == 22
	float3 node = m / 18.0;
	float3 corner = floor(node - 0.5);
	float nearest = 2.0;
	[unroll]
	for (int x = 0; x < 2; ++x)
	{
		[unroll]
		for (int y = 0; y < 2; ++y)
		{
			[unroll]
			for (int z = 0; z < 2; ++z)
			{
				float3 cell = corner + float3(x, y, z);
				float seed = Hash(cell);
				float3 phase = frac(seed * float3(1.0, 7.31, 13.7)) * 6.2831853;
				float3 center = cell + 0.5 + 0.4 * sin(time * (0.6 + seed * 0.5) + phase);
				nearest = min(nearest, length(node - center));
			}
		}
	}
	float link = pow(saturate(1.0 - nearest / 0.6), 2.0);
	color = lerp(primary * 0.45, secondary * 1.6, link) + secondary * rim * reflectivity * 0.3;
	alpha *= saturate(0.2 + link + rim * 0.3);
#elif EFFECT == 23
	float3 marbleSpace = m * 0.035;
	float3 warp = float3(Noise(marbleSpace + float3(0.0, time * 0.12, 0.0)), Noise(marbleSpace + float3(5.2, 1.3, time * 0.1)), 0.0);
	float swirl = Noise(marbleSpace * 1.7 + warp * 2.5 + float3(0.0, time * 0.18, 0.0));
	float thickness = swirl * 1.4 + m.z * 0.012 + edge * 0.5 - time * 0.05;
	float3 film = 0.5 + 0.5 * cos(6.2831853 * (thickness * float3(1.0, 1.15, 1.3) + float3(0.0, 0.3, 0.6)));
	float window = smoothstep(0.9, 0.92, dot(n, normalize(v + float3(0.0, 0.0, 1.1))));
	float bounce = smoothstep(0.95, 0.97, dot(n, normalize(v - float3(0.0, 0.0, 0.9)))) * 0.6;
	color = Aero(lerp(float3(0.85, 0.92, 1.0), film, 0.75) * primary * 1.2, (window + bounce) * secondary, edge);
#elif EFFECT == 24
	float auroraWarp = Noise(m * 0.03 + float3(0.0, time * 0.08, 0.0)) * 3.0;
	float bandA = pow(1.0 - abs(sin(dot(m, float3(0.04, 0.02, 0.07)) + auroraWarp - time * 0.7)), 24.0);
	float bandB = pow(1.0 - abs(sin(dot(m, float3(-0.03, 0.05, 0.06)) + auroraWarp * 1.3 - time * 0.5 + 1.7)), 30.0);
	color = Aero(lerp(primary, secondary, 0.5) * 0.25, bandA * primary * 1.4 + bandB * secondary * 1.4, edge);
#elif EFFECT == 25
	float3 water = m * 0.06 + float3(time * 0.2, 0.0, time * 0.15);
	float caustic = pow(saturate(1.0 - abs(Noise(water) - Noise(water * 1.4 + float3(5.2, 1.3, -time * 0.25))) * 3.0), 10.0);
	float shaft = pow(0.5 + 0.5 * sin(m.x * 0.18 + m.y * 0.11 + Noise(m * 0.02 + float3(0.0, 0.0, time * 0.1)) * 4.0 + time * 0.6), 6.0) * smoothstep(10.0, 80.0, m.z);
	float3 deep = lerp(primary * 0.2, primary, n.z * 0.5 + 0.5) * 1.3;
	color = Aero(deep, (caustic * 0.8 + shaft * 0.4) * secondary, edge);
#elif EFFECT == 26
	float height = m.z + (Noise(m * 0.08) - 0.5) * 6.0;
	float above = smoothstep(30.0, 34.0, height);
	float cloud = Noise(m * 0.07 + float3(time * 0.8, 0.0, 0.0)) * 0.65 + Noise(m * 0.16 + float3(time * 1.3, 3.0, 0.0)) * 0.35;
	float3 air = lerp(primary, primary * 0.25, smoothstep(30.0, 85.0, height));
	float3 grass = lerp(secondary * 0.45, secondary, Noise(m * 0.35));
	float gloss = smoothstep(0.8, 0.88, dot(n, normalize(v + float3(0.0, 0.0, 1.1)))) * 0.3;
	color = Aero(lerp(grass, air, above) * 1.2, smoothstep(0.55, 0.72, cloud) * above * 0.45 + gloss, edge);
#elif EFFECT == 27
	color = primary * 5.5 * SkyLight(n, facing) * (1.0 - pow(facing, 0.75)) + secondary * rim * reflectivity;
	alpha *= saturate(rim * 1.5 + 0.1);
#elif EFFECT == 28
	float3 galaxySpace = p * 0.8;
	float dust = Fbm(galaxySpace + float3(0.0, 0.0, time * 0.05) + Noise(galaxySpace * 1.7 - time * 0.03) * 1.5);
	float lane = pow(saturate(1.0 - abs(dust - 0.5) * 3.0), 3.0);
	float3 starCell = floor(p * 10.0);
	float starSeed = Hash(starCell);
	float star = step(0.97, starSeed) * pow(saturate(1.0 - length(frac(p * 10.0) - 0.5) * 2.5), 3.0) * (0.6 + 0.4 * sin(time * 3.0 + starSeed * 40.0));
	color = lerp(primary * 0.04, primary, dust) + secondary * lane * 1.5 + star * 4.0 + secondary * rim * reflectivity;
#elif EFFECT == 29
	float2 glyphSpace = Planar(p, ln) * 4.0;
	float column = floor(glyphSpace.x);
	float columnSeed = Hash(float3(column, 0.0, 1.0));
	float trail = frac(columnSeed + glyphSpace.y * 0.06 + time * (0.25 + columnSeed * 0.35));
	float2 glyphCell = frac(glyphSpace);
	float2 bitCell = floor(glyphCell * float2(3.0, 5.0));
	float bit = step(0.45, Hash(float3(column * 7.0 + bitCell.x, floor(glyphSpace.y) * 11.0 + bitCell.y, floor(time * 4.0 + columnSeed * 9.0))));
	float shape = step(0.12, glyphCell.x) * step(glyphCell.x, 0.88) * step(0.08, glyphCell.y) * step(glyphCell.y, 0.92);
	float head = smoothstep(0.95, 1.0, trail);
	color = primary * 0.05 + secondary * bit * shape * pow(trail, 4.0) * 1.6 + head * bit * shape * 2.0 + secondary * rim * 0.3;
#elif EFFECT == 30
	float3 flameSpace = p * float3(1.2, 1.2, 0.7) - float3(0.0, 0.0, time * 1.4);
	float flame = Fbm(flameSpace + Noise(flameSpace * 2.0 + time * 0.5) * 0.6);
	flame = saturate(flame * 1.7 - 0.45 + rim * 0.6);
	float3 fire = flame < 0.5 ? primary * flame * 2.0 : lerp(primary, secondary, flame * 2.0 - 1.0);
	color = fire * 1.6 + pow(flame, 4.0) * float3(1.0, 0.95, 0.8);
	alpha *= saturate(flame * 1.5 + 0.15);
#elif EFFECT == 31
	float2 crystal = Voronoi(Planar(p, ln) * 1.5, 0.0);
	float crackWidth = max(fwidth(crystal.y), 0.01);
	float crack = 1.0 - smoothstep(0.02, 0.02 + crackWidth * 2.0, crystal.y);
	float frost = Fbm(p * 3.0);
	float sparkle = pow(Hash(floor(p * 40.0) + floor(time * 2.0)), 60.0) * (0.3 + specular);
	color = lerp(primary * 0.6, secondary, crystal.x * 0.6 + frost * 0.4) * light + sky * fresnel * 0.6 + crack * secondary * 0.8 + sparkle * 6.0 + specular;
	alpha *= saturate(0.65 + 0.35 * rim + crack * 0.2);
#elif EFFECT == 32
	float diffuse = dot(n, normalize(float3(-0.4, -0.3, 0.85))) * 0.5 + 0.5;
	float bandWidth = max(fwidth(diffuse) * 1.5, 0.01);
	float bands = (smoothstep(0.33 - bandWidth, 0.33 + bandWidth, diffuse) + smoothstep(0.66 - bandWidth, 0.66 + bandWidth, diffuse)) * 0.5;
	float toonSpecular = smoothstep(0.96 - roughness * 0.1, 0.97 - roughness * 0.1, dot(n, halfVector)) * reflectivity;
	float inkWidth = max(fwidth(facing), 0.01);
	float ink = 1.0 - smoothstep(0.22 - inkWidth, 0.22 + inkWidth, facing);
	color = lerp(primary * lerp(0.35, 1.0, bands) + toonSpecular * 0.6, secondary, ink);
#elif EFFECT == 33
	float2 cells = Voronoi(Planar(p, ln) * 1.2, time * 0.8);
	float cellWidth = max(fwidth(cells.y), 0.01);
	float border = 1.0 - smoothstep(0.03, 0.03 + cellWidth * 2.0, cells.y);
	float core = pow(saturate(1.0 - cells.x), 3.0);
	color = primary * (0.15 + core * 0.6) * light + secondary * border * 2.0 + secondary * rim * reflectivity * 0.5;
#elif EFFECT == 34
	float2 board = Planar(p, ln) * 3.0;
	float2 boardCell = floor(board);
	float2 boardPosition = frac(board);
	float route = Hash(float3(boardCell, 7.0));
	float traceWidth = max(fwidth(board.x) + fwidth(board.y), 0.01);
	float horizontal = 1.0 - smoothstep(0.05, 0.05 + traceWidth, abs(boardPosition.y - 0.5));
	float vertical = 1.0 - smoothstep(0.05, 0.05 + traceWidth, abs(boardPosition.x - 0.5));
	float trace = route < 0.33 ? horizontal : (route < 0.66 ? vertical : max(horizontal * step(boardPosition.x, 0.5), vertical * step(boardPosition.y, 0.5)));
	float pad = (1.0 - smoothstep(0.1, 0.1 + traceWidth, length(boardPosition - 0.5))) * step(0.75, Hash(float3(boardCell, 3.0)));
	float current = pow(frac((boardCell.x + boardCell.y) * 0.13 - time * 0.6 + route), 12.0);
	color = primary * 0.15 * light + secondary * (max(trace, pad) * (0.35 + current * 2.5) + pad * 0.6) + sky * fresnel * 0.15;
#elif EFFECT == 35
	float3 arcSpace = p * 1.4;
	float arcTime = time * 3.0;
	float arc = abs(Noise(arcSpace + float3(0.0, 0.0, arcTime)) + Noise(arcSpace * 2.1 - float3(arcTime * 0.7, 0.0, 0.0)) * 0.5 - 0.75);
	float bolt = pow(saturate(1.0 - arc * 8.0), 6.0);
	float flicker = 0.4 + 0.6 * step(0.3, Noise(float3(time * 12.0, 0.0, 0.0)));
	color = primary * 0.1 + secondary * bolt * 3.0 * flicker + primary * rim * 1.2 + bolt * bolt;
	alpha *= saturate(0.35 + bolt + rim);
#elif EFFECT == 36
	float3 spectrum = Hue(dot(p, float3(0.3, 0.2, 0.6)) + time * 0.15 + edge * 0.3);
	color = spectrum * primary * light * 1.1 + specular * reflectivity + sky * fresnel * 0.25;
#elif EFFECT == 37
	float3 stone = p * 0.9;
	float veinWarp = Fbm(stone * 1.5);
	float veins = pow(1.0 - abs(sin(stone.x * 2.0 + stone.z * 1.3 + veinWarp * 6.0)), 10.0);
	float3 slab = lerp(primary, primary * 0.82, veinWarp);
	color = lerp(slab, secondary, veins * 0.85) * light + sky * fresnel * reflectivity * 0.6 + specular * (1.0 - roughness);
#elif EFFECT == 38
	float thermal = saturate(facing * 0.75 + Noise(p * 0.8 + time * 0.1) * 0.35 - 0.05);
	float3 iron = thermal < 0.25 ? lerp(float3(0.0, 0.0, 0.1), float3(0.3, 0.0, 0.6), thermal * 4.0)
		: (thermal < 0.5 ? lerp(float3(0.3, 0.0, 0.6), float3(0.9, 0.1, 0.1), thermal * 4.0 - 1.0)
		: (thermal < 0.75 ? lerp(float3(0.9, 0.1, 0.1), float3(1.0, 0.7, 0.0), thermal * 4.0 - 2.0)
		: lerp(float3(1.0, 0.7, 0.0), float3(1.0, 1.0, 0.9), thermal * 4.0 - 3.0)));
	color = iron * 1.2;
#elif EFFECT == 39
	float2 camoSpace = floor(Planar(p, ln) * 16.0) / 16.0;
	float camo = Fbm(float3(camoSpace * 1.3, 3.1));
	float3 tone = camo < 0.42 ? primary * 0.5 : (camo < 0.52 ? primary : (camo < 0.6 ? secondary : secondary * 0.5));
	color = tone * light + specular * reflectivity * 0.3;
#endif
	color = max(color * intensity, 0.0);
	if (g_Animation.z > 1.5)
		color *= alpha;
	return float4(color, alpha);
}
#endif
