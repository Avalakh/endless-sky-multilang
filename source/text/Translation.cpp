/* Translation.cpp
Copyright (c) 2025

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Translation.h"

#include "Files.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace std;

namespace {

	string currentLanguage;
	map<string, string> currentStrings;
	map<string, string> fallbackStrings;
	bool fallbackLoaded = false;

	filesystem::path MainUiLanguageDir()
	{
		return Files::UserPlugins() / "ru-data-translation" / "mainUI";
	}

	void AppendUtf8FromCodePoint(string &out, uint32_t cp)
	{
		if(cp <= 0x7F)
			out += static_cast<char>(cp);
		else if(cp <= 0x7FF)
		{
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else if(cp <= 0xFFFF)
		{
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
		else if(cp <= 0x10FFFF)
		{
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}

	void DecodeEscapeSequence(const string &in, size_t &pos, string &out)
	{
		if(pos >= in.length())
			return;
		char c = in[pos++];
		switch(c)
		{
			case '"':  out += '"';  break;
			case '\\': out += '\\'; break;
			case '/':  out += '/';  break;
			case 'b':  out += '\b'; break;
			case 'f':  out += '\f'; break;
			case 'n':  out += '\n'; break;
			case 'r':  out += '\r'; break;
			case 't':  out += '\t'; break;
			case 'u':
			{
				uint32_t cp = 0;
				for(int i = 0; i < 4 && pos < in.length(); ++i)
				{
					char h = in[pos];
					int d = 0;
					if(h >= '0' && h <= '9') d = h - '0';
					else if(h >= 'a' && h <= 'f') d = h - 'a' + 10;
					else if(h >= 'A' && h <= 'F') d = h - 'A' + 10;
					else break;
					cp = (cp << 4) | d;
					++pos;
				}
				AppendUtf8FromCodePoint(out, cp);
				break;
			}
			default:
				out += c;
				break;
		}
	}

	bool ParseJsonString(const string &data, size_t &pos, string &out)
	{
		out.clear();
		while(pos < data.length() && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' || data[pos] == '\r'))
			++pos;
		if(pos >= data.length() || data[pos] != '"')
			return false;
		++pos;
		while(pos < data.length())
		{
			char c = data[pos];
			if(c == '"')
			{
				++pos;
				return true;
			}
			if(c == '\\')
			{
				++pos;
				DecodeEscapeSequence(data, pos, out);
				continue;
			}
			out += c;
			++pos;
		}
		return false;
	}

	bool ParseFlatJson(const string &data, map<string, string> &out)
	{
		out.clear();
		size_t pos = 0;
		while(pos < data.length() && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' || data[pos] == '\r'))
			++pos;
		if(pos >= data.length() || data[pos] != '{')
			return false;
		++pos;
		for(;;)
		{
			while(pos < data.length() && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == ','))
				++pos;
			if(pos >= data.length())
				return false;
			if(data[pos] == '}')
			{
				++pos;
				return true;
			}
			string key;
			if(!ParseJsonString(data, pos, key))
				return false;
			while(pos < data.length() && (data[pos] == ' ' || data[pos] == '\t' || data[pos] == '\n' || data[pos] == '\r'))
				++pos;
			if(pos >= data.length() || data[pos] != ':')
				return false;
			++pos;
			string value;
			if(!ParseJsonString(data, pos, value))
				return false;
			out[key] = value;
		}
	}

	void LoadInto(const string &languageCode, map<string, string> &target)
	{
		target.clear();
		filesystem::path langRoot = MainUiLanguageDir();
		filesystem::path langDirPath = langRoot / languageCode;

		if(Files::Exists(langDirPath) && filesystem::is_directory(langDirPath))
		{
			vector<filesystem::path> entries = Files::RecursiveList(langDirPath);
			for(const auto &entry : entries)
			{
				if(entry.extension() == ".json")
				{
					string data = Files::Read(entry);
					map<string, string> partial;
					ParseFlatJson(data, partial);
					for(const auto &p : partial)
						target[p.first] = p.second;
				}
			}
		}
	}

}

namespace Translation {

	void Load(const string &languageCode)
	{
		LoadInto(languageCode, currentStrings);
	}

	void SetLanguage(const string &code)
	{
		currentLanguage = code;
		Load(code);
	}

	string Tr(const string &key)
	{
		auto it = currentStrings.find(key);
		if(it != currentStrings.end())
			return it->second;
		if(!fallbackLoaded && currentLanguage != "en")
		{
			LoadInto("en", fallbackStrings);
			fallbackLoaded = true;
		}
		auto fit = fallbackStrings.find(key);
		if(fit != fallbackStrings.end())
			return fit->second;
		return key;
	}

	string TrCategory(const string &category)
	{
		string key = "category." + category;
		string value = Tr(key);
		return (value == key) ? category : value;
	}

	string TrFormation(const string &formationName)
	{
		string key = "formation." + formationName;
		string value = Tr(key);
		return (value == key) ? formationName : value;
	}

	string TrGovernment(const string &displayName)
	{
		string key = "government." + displayName;
		string value = Tr(key);
		return (value == key) ? displayName : value;
	}

	string TrStartName(const string &identifier, const string &fallback)
	{
		string key = "start.name." + identifier;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrStartDescription(const string &identifier, const string &fallback)
	{
		string key = "start.desc." + identifier;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrPhrase(const string &phraseName, const string &fallback)
	{
		string key = "phrase." + phraseName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrMissionDescription(const string &identifier, const string &fallback)
	{
		string key = "mission.desc." + identifier;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrSubstitutionValue(const string &key, const string &value)
	{
		if(key == "<commodity>")
		{
			string t = Tr("commodity." + value);
			return (t == "commodity." + value) ? value : t;
		}
		if(key == "<government>")
			return TrGovernment(value);
		if(key == "<home planet>" || key == "<planet>")
		{
			string t = Tr("planet.name." + value);
			return (t == "planet.name." + value) ? value : t;
		}
		if(key == "<home system>" || key == "<system>")
		{
			string t = Tr("system.name." + value);
			return (t == "system.name." + value) ? value : t;
		}
		return value;
	}

	void TranslateSubstitutionValues(map<string, string> &subs)
	{
		for(auto &p : subs)
			p.second = TrSubstitutionValue(p.first, p.second);
	}

	string TrSeries(const string &seriesName)
	{
		string key = "series." + seriesName;
		string value = Tr(key);
		return (value == key) ? seriesName : value;
	}

	string TrOutfitName(const string &trueName, const string &fallback)
	{
		string key = "outfit.name." + trueName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrOutfitDescription(const string &trueName, const string &fallback)
	{
		string key = "outfit.desc." + trueName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrOutfitPluralName(const string &trueName, const string &fallback)
	{
		string key = "outfit.plural." + trueName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrShipName(const string &trueModelName, const string &fallback)
	{
		string key = "ship.name." + trueModelName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrShipPluralName(const string &trueModelName, const string &fallback)
	{
		string key = "ship.plural." + trueModelName;
		string value = Tr(key);
		return (value == key) ? fallback : value;
	}

	string TrShipDescription(const string &trueModelName, const string &fallback)
	{
		string key = "ship.desc." + trueModelName;
		string value = Tr(key);
		if(value == key || value.empty())
			return fallback;
		return value;
	}

	string TrPlanetDescription(const string &planetTrueName, const string &fallback)
	{
		string key = "planet.desc." + planetTrueName;
		string value = Tr(key);
		if(value == key || value.empty())
			return fallback;
		return value;
	}

	string TrSpaceportDescription(const string &planetTrueName, const string &fallback)
	{
		string key = "planet.spaceport." + planetTrueName;
		string value = Tr(key);
		if(value == key || value.empty())
			return fallback;
		return value;
	}

	string Tr(const string &key, const map<string, string> &replacements)
	{
		string s = Tr(key);
		for(const auto &p : replacements)
		{
			string placeholder = "{{" + p.first + "}}";
			size_t pos = 0;
			for(;;)
			{
				pos = s.find(placeholder, pos);
				if(pos == string::npos)
					break;
				s.replace(pos, placeholder.length(), p.second);
				pos += p.second.length();
			}
		}
		return s;
	}

	vector<string> AvailableLanguageCodes()
	{
		vector<string> codes;
		filesystem::path langDir = MainUiLanguageDir();
		if(!Files::Exists(langDir))
			return codes;

		set<string> fromDirs;

		for(const auto &subdir : Files::ListDirectories(langDir))
		{
			string code = subdir.filename().string();
			vector<filesystem::path> files = Files::RecursiveList(langDir / code);
			bool hasJson = false;
			for(const auto &f : files)
			{
				if(f.extension() == ".json")
				{
					hasJson = true;
					break;
				}
			}
			if(hasJson)
			{
				codes.push_back(code);
				fromDirs.insert(code);
			}
		}

		sort(codes.begin(), codes.end());
		auto it = find(codes.begin(), codes.end(), "en");
		if(it == codes.end())
			codes.insert(codes.begin(), "en");
		else if(it != codes.begin())
		{
			codes.erase(it);
			codes.insert(codes.begin(), "en");
		}
		return codes;
	}

}
