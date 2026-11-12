#pragma once

#include <cctype>
#include <filesystem>
#include <string>

namespace FileName
{
	inline bool IsSafe(const std::string& sName)
	{
		if (sName.empty() || sName == "." || sName == ".." || sName.size() > 128)
			return false;

		for (unsigned char c : sName)
		{
			if (c < 32 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*')
				return false;
		}
		if (sName.back() == '.' || sName.back() == ' ')
			return false;

		std::string sDevice = sName.substr(0, sName.find('.'));
		for (char& c : sDevice)
			c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

		if (sDevice == "CON" || sDevice == "PRN" || sDevice == "AUX" || sDevice == "NUL")
			return false;
		if (sDevice.size() == 4 && (sDevice.starts_with("COM") || sDevice.starts_with("LPT")) &&
			sDevice.back() >= '1' && sDevice.back() <= '9')
			return false;

		return std::filesystem::path(sName).filename() == sName;
	}
}
