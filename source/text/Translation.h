/* Translation.h
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

#pragma once

#include <map>
#include <string>
#include <vector>

namespace Translation {

	void Load(const std::string &languageCode);
	void SetLanguage(const std::string &code);
	std::string Tr(const std::string &key);
	std::string Tr(const std::string &key, const std::map<std::string, std::string> &replacements);

	/// Return translated category name (e.g. outfit/ship category). Key is "category." + category.
	/// If no translation exists, returns the original category string.
	std::string TrCategory(const std::string &category);

	/// Return translated formation name. Key is "formation." + formationName.
	/// If no translation exists, returns the original formation string.
	std::string TrFormation(const std::string &formationName);

	/// Return translated government display name. Key is "government." + displayName.
	/// If no translation exists, returns the original string.
	std::string TrGovernment(const std::string &displayName);

	/// Return translated start scenario name. Key is "start.name." + identifier.
	/// If no translation exists, returns the fallback.
	std::string TrStartName(const std::string &identifier, const std::string &fallback);

	/// Return translated start scenario description. Key is "start.desc." + identifier.
	/// If no translation exists, returns the fallback.
	std::string TrStartDescription(const std::string &identifier, const std::string &fallback);

	/// Return translated phrase. Key is "phrase." + phraseName. Placeholders (e.g. <planet>, <npc>)
	/// must be preserved in the translation.
	/// If no translation exists, returns the fallback.
	std::string TrPhrase(const std::string &phraseName, const std::string &fallback);

	/// Return translated mission description. Key is "mission.desc." + identifier.
	/// If no translation exists, returns the fallback.
	std::string TrMissionDescription(const std::string &identifier, const std::string &fallback);

	/// Return translated substitution value based on key. Uses Tr for known keys (commodity, government,
	/// planet.name, system.name). Returns value unchanged for unknown keys.
	std::string TrSubstitutionValue(const std::string &key, const std::string &value);

	/// Translate values in the substitution map for known keys. Modifies subs in-place.
	void TranslateSubstitutionValues(std::map<std::string, std::string> &subs);

	/// Return translated series name. Key is "series." + seriesName. Used for outfit/ship series in UI.
	std::string TrSeries(const std::string &seriesName);

	/// Return translated outfit display name. Key is "outfit.name." + trueName.
	std::string TrOutfitName(const std::string &trueName, const std::string &fallback);
	/// Return translated outfit description. Key is "outfit.desc." + trueName.
	std::string TrOutfitDescription(const std::string &trueName, const std::string &fallback);
	/// Return translated outfit plural name. Key is "outfit.plural." + trueName.
	std::string TrOutfitPluralName(const std::string &trueName, const std::string &fallback);

	/// Return translated ship model display name. Key is "ship.name." + trueModelName.
	std::string TrShipName(const std::string &trueModelName, const std::string &fallback);
	/// Return translated ship model plural name. Key is "ship.plural." + trueModelName.
	std::string TrShipPluralName(const std::string &trueModelName, const std::string &fallback);
	/// Return translated ship description. Key is "ship.desc." + trueModelName.
	std::string TrShipDescription(const std::string &trueModelName, const std::string &fallback);

	/// Return translated planet description. Key is "planet.desc." + planetTrueName.
	std::string TrPlanetDescription(const std::string &planetTrueName, const std::string &fallback);
	/// Return translated spaceport description. Key is "planet.spaceport." + planetTrueName.
	std::string TrSpaceportDescription(const std::string &planetTrueName, const std::string &fallback);

	/// List of language codes for which language/<code>/ or language/<code>.json exists (e.g. "en", "ru").
	std::vector<std::string> AvailableLanguageCodes();

}
