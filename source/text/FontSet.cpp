/* FontSet.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "FontSet.h"

#include "Font.h"
#include "../Preferences.h"

#include <map>
#include <utility>

using namespace std;

namespace {
	map<pair<int, string>, Font> fonts;
}



void FontSet::Add(const filesystem::path &path, int size, const string &languageCode)
{
	auto key = make_pair(size, languageCode);
	if(!fonts.contains(key))
		fonts[key].Load(path);
}



void FontSet::AddTtf(const filesystem::path &ttfPath, int size, const string &languageCode)
{
	auto key = make_pair(size, languageCode);
	fonts[key].LoadFromTtf(ttfPath, size);
}



const Font &FontSet::Get(int size)
{
	string lang = Preferences::Language();
	auto key = make_pair(size, lang);
	if(fonts.contains(key))
		return fonts[key];
	key = make_pair(size, string("en"));
	if(fonts.contains(key))
		return fonts[key];
	for(const auto &p : fonts)
		if(p.first.first == size)
			return p.second;
	return fonts.begin()->second;
}
