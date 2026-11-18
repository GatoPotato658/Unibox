sampler2D Source : register(s0);
sampler2D Previous : register(s1);
float4 Direction : register(c0);
float4 Settings : register(c1);
float4 Tint : register(c2);
float4 Composite : register(c3);

float4 Decode(float2 uv : TEXCOORD0) : COLOR0
{
	float4 result = tex2D(Source, uv);
	result.a = pow(saturate(result.a), Direction.z);
	return result;
}

float4 Dilate(float2 uv : TEXCOORD0) : COLOR0
{
	float4 result = tex2D(Source, uv);
	[unroll]
	for (int i = -16; i <= 16; ++i)
	{
		if (abs(i) <= Direction.z)
		{
			float4 sampleValue = tex2D(Source, uv + Direction.xy * i);
			if (sampleValue.a > result.a)
				result = sampleValue;
		}
	}
	return result;
}

float4 Blur(float2 uv : TEXCOORD0) : COLOR0
{
	float4 result = tex2D(Source, uv) * 0.2270270270;
	result += tex2D(Source, uv + Direction.xy * 1.3846153846) * 0.3162162162;
	result += tex2D(Source, uv - Direction.xy * 1.3846153846) * 0.3162162162;
	result += tex2D(Source, uv + Direction.xy * 3.2307692308) * 0.0702702703;
	result += tex2D(Source, uv - Direction.xy * 3.2307692308) * 0.0702702703;
	return result;
}

float4 Combine(float2 uv : TEXCOORD0) : COLOR0
{
	float4 sourceValue = tex2D(Source, uv);
	float previousAlpha = tex2D(Previous, uv).a;
	float coverage = saturate(sourceValue.a - previousAlpha * Composite.x);
	float3 color = sourceValue.rgb / max(sourceValue.a, 0.0001);
	color = lerp(color, Tint.rgb, Tint.a);
	float opacity = saturate(coverage * Settings.y * Settings.z * Composite.y);
	return float4(color * Settings.x * opacity, opacity);
}
