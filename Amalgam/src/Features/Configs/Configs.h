#pragma once
#include "../../SDK/SDK.h"

#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>

class CConfigs
{
public:
	CConfigs();

	bool SaveConfig(const std::string& sConfigName, bool bNotify = true);
	bool LoadConfig(const std::string& sConfigName, bool bNotify = true);
	void DeleteConfig(const std::string& sConfigName, bool bNotify = true);
	void ResetConfig(const std::string& sConfigName, bool bNotify = true);
	bool SaveVisual(const std::string& sConfigName, bool bNotify = true);
	bool LoadVisual(const std::string& sConfigName, bool bNotify = true);
	void DeleteVisual(const std::string& sConfigName, bool bNotify = true);
	void ResetVisual(const std::string& sConfigName, bool bNotify = true);

	template <class T> void SaveJson(boost::property_tree::ptree& t, const std::string& s, const T& v);
	template <class T> void LoadJson(const boost::property_tree::ptree& t, const std::string& s, T& v);
	template <class T> void LoadJson(const boost::property_tree::ptree& t, const std::string& s, ConfigVar<T>* c, int i);

	std::string m_sCurrentConfig = "default";
	std::string m_sCurrentVisuals = "default";
	std::string m_sConfigPath;
	std::string m_sVisualsPath;
	std::string m_sCorePath;
	std::string m_sMaterialsPath;
	const std::string m_sConfigExtension = ".json";
};

ADD_FEATURE(CConfigs, Configs);
