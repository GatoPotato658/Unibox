param(
	[string]$Fxc = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "GlowPost.hlsl"
$header = Join-Path $PSScriptRoot "GlowShaders.h"
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add("#pragma once")
$lines.Add("#include <cstdint>")
foreach ($entry in @("Decode", "Dilate", "Blur", "Combine"))
{
	$binary = [System.IO.Path]::GetTempFileName()
	try
	{
		& $Fxc /nologo /T ps_3_0 /E $entry /O3 /Fo $binary $source
		if ($LASTEXITCODE -ne 0) { throw "fxc failed for $entry" }
		$bytes = [System.IO.File]::ReadAllBytes($binary)
		$lines.Add("inline constexpr std::uint32_t g_Glow$entry[] =")
		$lines.Add("{")
		for ($offset = 0; $offset -lt $bytes.Length; $offset += 32)
		{
			$words = @()
			for ($i = $offset; $i -lt [Math]::Min($offset + 32, $bytes.Length); $i += 4)
			{
				$words += "0x{0:X8}" -f [System.BitConverter]::ToUInt32($bytes, $i)
			}
			$lines.Add("`t" + ($words -join ", ") + ",")
		}
		$lines.Add("};")
	}
	finally
	{
		Remove-Item $binary -ErrorAction SilentlyContinue
	}
}
[System.IO.File]::WriteAllText($header, ($lines -join "`n") + "`n", [System.Text.UTF8Encoding]::new($false))
