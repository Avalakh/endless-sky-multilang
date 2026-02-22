/* ShipInfoDisplay.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "ShipInfoDisplay.h"

#include "text/Alignment.h"
#include "CategoryList.h"
#include "CategoryType.h"
#include "Color.h"
#include "Depreciation.h"
#include "shader/FillShader.h"
#include "text/Format.h"
#include "text/Translation.h"
#include "GameData.h"
#include "Outfit.h"
#include "PlayerInfo.h"
#include "Ship.h"
#include "text/Table.h"
#include "Weapon.h"

#include <algorithm>
#include <map>
#include <sstream>

using namespace std;



ShipInfoDisplay::ShipInfoDisplay(const Ship &ship, const PlayerInfo &player, bool descriptionCollapsed)
{
	Update(ship, player, descriptionCollapsed);
}



// Call this every time the ship changes.
// Panels that have scrolling abilities are not limited by space, allowing more detailed attributes.
void ShipInfoDisplay::Update(const Ship &ship, const PlayerInfo &player, bool descriptionCollapsed, bool scrollingPanel)
{
	UpdateDescription(ship.TranslatedDescription(), ship.Attributes().Licenses(), true);
	UpdateAttributes(ship, player, descriptionCollapsed, scrollingPanel);
	const Depreciation &depreciation = ship.IsYours() ? player.FleetDepreciation() : player.StockDepreciation();
	UpdateOutfits(ship, player, depreciation);

	maximumHeight = max(descriptionHeight, max(attributesHeight, outfitsHeight));
}



int ShipInfoDisplay::GetAttributesHeight(bool sale) const
{
	return attributesHeight + (sale ? saleHeight : 0);
}



int ShipInfoDisplay::OutfitsHeight() const
{
	return outfitsHeight;
}



void ShipInfoDisplay::DrawAttributes(const Point &topLeft) const
{
	DrawAttributes(topLeft, false);
}



// Draw each of the panels.
void ShipInfoDisplay::DrawAttributes(const Point &topLeft, const bool sale) const
{
	// Header.
	Point point = Draw(topLeft, attributeHeaderLabels, attributeHeaderValues, &attributeHeaderTooltipKeys);

	// Sale info.
	if(sale)
	{
		point = Draw(point, saleLabels, saleValues, &saleTooltipKeys);

		const Color &color = *GameData::Colors().Get("medium");
		FillShader::Fill(point + Point(.5 * WIDTH, 5.), Point(WIDTH - 20., 1.), color);
	}
	else
		point -= Point(0, 10.);

	// Body.
	point = Draw(point, attributeLabels, attributeValues, &attributeTooltipKeys);

	// Get standard colors to draw with.
	const Color &labelColor = *GameData::Colors().Get("medium");
	const Color &valueColor = *GameData::Colors().Get("bright");

	Table table;
	table.AddColumn(10, {WIDTH - 10, Alignment::LEFT});
	table.AddColumn(WIDTH - 90, {WIDTH - 80, Alignment::RIGHT});
	table.AddColumn(WIDTH - 10, {WIDTH - 20, Alignment::RIGHT});
	table.SetHighlight(0, WIDTH);
	table.DrawAt(point);
	table.DrawGap(10.);

	table.Advance();
	table.Draw(Translation::Tr("ui.energy"), labelColor);
	table.Draw(Translation::Tr("ui.heat"), labelColor);

	for(unsigned i = 0; i < tableLabels.size(); ++i)
	{
		CheckHover(table, tableTooltipKeys[i]);
		table.Draw(Translation::Tr(tableLabels[i]), labelColor);
		table.Draw(energyTable[i], valueColor);
		table.Draw(heatTable[i], valueColor);
	}
}



void ShipInfoDisplay::DrawOutfits(const Point &topLeft) const
{
	Draw(topLeft, outfitLabels, outfitValues, &outfitTooltipKeys);
}



void ShipInfoDisplay::UpdateAttributes(const Ship &ship, const PlayerInfo &player, bool descriptionCollapsed,
		bool scrollingPanel)
{
	bool isGeneric = ship.GivenName().empty() || ship.GetPlanet();

	attributeHeaderLabels.clear();
	attributeHeaderValues.clear();
	attributeHeaderTooltipKeys.clear();

	attributeHeaderLabels.push_back(Translation::Tr("ship_info.model"));
	attributeHeaderTooltipKeys.push_back("model:");
	attributeHeaderValues.push_back(ship.TranslatedDisplayModelName());

	attributesHeight = 20;

	// Only show the ship category on scrolling panels with no risk of overflow.
	if(scrollingPanel)
	{
		attributeHeaderLabels.push_back(Translation::Tr("ship_info.category"));
		attributeHeaderTooltipKeys.push_back("category:");
		const string &category = ship.BaseAttributes().Category();
		attributeHeaderValues.push_back(category.empty() ? "???" : Translation::TrCategory(category));
		attributesHeight += 20;
	}

	attributeLabels.clear();
	attributeValues.clear();
	attributeTooltipKeys.clear();
	attributesHeight += 20;

	const Outfit &attributes = ship.Attributes();

	if(!ship.IsYours())
		for(const string &license : attributes.Licenses())
		{
			if(player.HasLicense(license))
				continue;

			const auto &licenseOutfit = GameData::Outfits().Find(license + " License");
			if(descriptionCollapsed || (licenseOutfit && licenseOutfit->Cost()))
			{
				attributeLabels.push_back(Translation::Tr("ship_info.license"));
				attributeTooltipKeys.push_back("license:");
				attributeValues.push_back(license);
				attributesHeight += 20;
			}
		}

	int64_t fullCost = ship.Cost();
	const Depreciation &depreciation = ship.IsYours() ? player.FleetDepreciation() : player.StockDepreciation();
	int64_t depreciated = depreciation.Value(ship, player.GetDate().DaysSinceEpoch());
	if(depreciated == fullCost)
	{
		attributeLabels.push_back(Translation::Tr("ship_info.cost"));
		attributeTooltipKeys.push_back("cost:");
	}
	else
	{
		map<string, string> rep = {{"pct", to_string((100 * depreciated) / fullCost)}};
		attributeLabels.push_back(Translation::Tr("ship_info.cost_pct", rep));
		attributeTooltipKeys.push_back("cost (%):");
	}
	attributeValues.push_back(Format::AbbreviatedNumber(depreciated));
	attributesHeight += 20;

	attributeLabels.push_back(string());
	attributeValues.push_back(string());
	attributeTooltipKeys.push_back(string());
	attributesHeight += 10;
	double shieldRegen = (attributes.Get("shield generation")
		+ attributes.Get("delayed shield generation"))
		* (1. + attributes.Get("shield generation multiplier"));
	bool hasShieldRegen = shieldRegen > 0.;
	if(hasShieldRegen)
	{
		attributeLabels.push_back(Translation::Tr("ship_info.shields_charge"));
		attributeTooltipKeys.push_back("shields (charge):");
		attributeValues.push_back(Format::Number(ship.MaxShields())
			+ " (" + Format::Number(60. * shieldRegen) + "/s)");
	}
	else
	{
		attributeLabels.push_back(Translation::Tr("ship_info.shields"));
		attributeTooltipKeys.push_back("shields:");
		attributeValues.push_back(Format::Number(ship.MaxShields()));
	}
	attributesHeight += 20;
	double hullRepair = (attributes.Get("hull repair rate")
		+ attributes.Get("delayed hull repair rate"))
		* (1. + attributes.Get("hull repair multiplier"));
	bool hasHullRepair = hullRepair > 0.;
	if(hasHullRepair)
	{
		attributeLabels.push_back(Translation::Tr("ship_info.hull_repair"));
		attributeTooltipKeys.push_back("hull (repair):");
		attributeValues.push_back(Format::Number(ship.MaxHull())
			+ " (" + Format::Number(60. * hullRepair) + "/s)");
	}
	else
	{
		attributeLabels.push_back(Translation::Tr("ship_info.hull"));
		attributeTooltipKeys.push_back("hull:");
		attributeValues.push_back(Format::Number(ship.MaxHull()));
	}
	attributesHeight += 20;
	double emptyMass = attributes.Mass();
	double currentMass = ship.Mass();
	attributeLabels.push_back(Translation::Tr(isGeneric ? "ship_info.mass_with_no_cargo" : "ship_info.mass"));
	attributeTooltipKeys.push_back(isGeneric ? "mass with no cargo:" : "mass:");
	attributeValues.push_back(Format::Number(isGeneric ? emptyMass : currentMass) + " tons");
	attributesHeight += 20;
	attributeLabels.push_back(Translation::Tr(isGeneric ? "ship_info.cargo_space" : "ship_info.cargo"));
	attributeTooltipKeys.push_back(isGeneric ? "cargo space:" : "cargo:");
	if(isGeneric)
		attributeValues.push_back(Format::Number(attributes.Get("cargo space")) + " tons");
	else
		attributeValues.push_back(Format::Number(ship.Cargo().Used())
			+ " / " + Format::Number(attributes.Get("cargo space")) + " tons");
	attributesHeight += 20;
	attributeLabels.push_back(Translation::Tr("ship_info.required_crew_bunks"));
	attributeTooltipKeys.push_back("required crew / bunks:");
	attributeValues.push_back(Format::Number(ship.RequiredCrew())
		+ " / " + Format::Number(attributes.Get("bunks")));
	attributesHeight += 20;
	attributeLabels.push_back(Translation::Tr(isGeneric ? "ship_info.fuel_capacity" : "ship_info.fuel"));
	attributeTooltipKeys.push_back(isGeneric ? "fuel capacity:" : "fuel:");
	double fuelCapacity = attributes.Get("fuel capacity");
	if(isGeneric)
		attributeValues.push_back(Format::Number(fuelCapacity));
	else
		attributeValues.push_back(Format::Number(ship.Fuel() * fuelCapacity)
			+ " / " + Format::Number(fuelCapacity));
	attributesHeight += 20;

	double fullMass = emptyMass + attributes.Get("cargo space");
	isGeneric &= (fullMass != emptyMass);
	double forwardThrust = attributes.Get("thrust") ? attributes.Get("thrust") : attributes.Get("afterburner thrust");
	attributeLabels.push_back(string());
	attributeValues.push_back(string());
	attributeTooltipKeys.push_back(string());
	attributesHeight += 10;
	attributeLabels.push_back(Translation::Tr(isGeneric ? "ship_info.movement_full" : "ship_info.movement"));
	attributeTooltipKeys.push_back(isGeneric ? "movement (full - no cargo):" : "movement:");
	attributeValues.push_back(string());
	attributesHeight += 20;
	attributeLabels.push_back(Translation::Tr("ship_info.max_speed"));
	attributeTooltipKeys.push_back("max speed:");
	attributeValues.push_back(Format::Number(60. * forwardThrust / ship.Drag()));
	attributesHeight += 20;

	// Movement stats are influenced by inertia reduction.
	double reduction = 1. + attributes.Get("inertia reduction");
	emptyMass /= reduction;
	currentMass /= reduction;
	fullMass /= reduction;
	attributeLabels.push_back(Translation::Tr("ship_info.acceleration"));
	attributeTooltipKeys.push_back("acceleration:");
	double baseAccel = 3600. * forwardThrust * (1. + attributes.Get("acceleration multiplier"));
	if(!isGeneric)
		attributeValues.push_back(Format::Number(baseAccel / currentMass));
	else
		attributeValues.push_back(Format::Number(baseAccel / fullMass)
			+ " - " + Format::Number(baseAccel / emptyMass));
	attributesHeight += 20;

	attributeLabels.push_back(Translation::Tr("ship_info.turning"));
	attributeTooltipKeys.push_back("turning:");
	double baseTurn = 60. * attributes.Get("turn") * (1. + attributes.Get("turn multiplier"));
	if(!isGeneric)
		attributeValues.push_back(Format::Number(baseTurn / currentMass));
	else
		attributeValues.push_back(Format::Number(baseTurn / fullMass)
			+ " - " + Format::Number(baseTurn / emptyMass));
	attributesHeight += 20;

	// Find out how much outfit, engine, and weapon space the chassis has.
	map<string, double> chassis;
	static const vector<string> NAME_KEYS = {
		"ship_info.outfit_space_free", "outfit space",
		"ship_info.weapon_capacity", "weapon capacity",
		"ship_info.engine_capacity", "engine capacity",
		"ship_info.gun_ports_free", "gun ports",
		"ship_info.turret_mounts_free", "turret mounts"
	};
	static const vector<string> NAME_TOOLTIPS = {
		"outfit space free:", "    weapon capacity:", "    engine capacity:",
		"gun ports free:", "turret mounts free:"
	};
	for(unsigned i = 1; i < NAME_KEYS.size(); i += 2)
		chassis[NAME_KEYS[i]] = attributes.Get(NAME_KEYS[i]);
	for(const auto &it : ship.Outfits())
		for(auto &cit : chassis)
			cit.second -= min(0., it.second * it.first->Get(cit.first));

	attributeLabels.push_back(string());
	attributeValues.push_back(string());
	attributeTooltipKeys.push_back(string());
	attributesHeight += 10;
	for(unsigned i = 0; i < NAME_KEYS.size(); i += 2)
	{
		attributeLabels.push_back(Translation::Tr(NAME_KEYS[i]));
		attributeTooltipKeys.push_back(NAME_TOOLTIPS[i / 2]);
		attributeValues.push_back(Format::Number(attributes.Get(NAME_KEYS[i + 1]))
			+ " / " + Format::Number(chassis[NAME_KEYS[i + 1]]));
		attributesHeight += 20;
	}

	// Print the number of bays for each bay-type we have
	for(const auto &category : GameData::GetCategory(CategoryType::BAY))
	{
		const string &bayType = category.Name();
		int totalBays = ship.BaysTotal(bayType);
		if(totalBays)
		{
			string bayLabel = bayType;
			transform(bayLabel.begin(), bayLabel.end(), bayLabel.begin(), ::tolower);
			string label = bayLabel + " bays:";
			attributeLabels.emplace_back(label);
			attributeTooltipKeys.emplace_back(label);
			attributeValues.emplace_back(to_string(totalBays));
			attributesHeight += 20;
		}
	}

	tableLabels.clear();
	tableTooltipKeys.clear();
	energyTable.clear();
	heatTable.clear();
	// Skip a spacer and the table header.
	attributesHeight += 30;

	const double idleEnergyPerFrame = attributes.Get("energy generation")
		+ attributes.Get("solar collection")
		+ attributes.Get("fuel energy")
		- attributes.Get("energy consumption")
		- attributes.Get("cooling energy");
	const double idleHeatPerFrame = attributes.Get("heat generation")
		+ attributes.Get("solar heat")
		+ attributes.Get("fuel heat")
		- ship.CoolingEfficiency() * (attributes.Get("cooling") + attributes.Get("active cooling"));
	tableLabels.push_back("ship_info.idle");
	tableTooltipKeys.push_back("idle:");
	energyTable.push_back(Format::Number(60. * idleEnergyPerFrame));
	heatTable.push_back(Format::Number(60. * idleHeatPerFrame));

	// Add energy and heat while moving to the table.
	attributesHeight += 20;
	const double movingEnergyPerFrame =
		max(attributes.Get("thrusting energy"), attributes.Get("reverse thrusting energy"))
		+ attributes.Get("turning energy")
		+ attributes.Get("afterburner energy");
	const double movingHeatPerFrame = max(attributes.Get("thrusting heat"), attributes.Get("reverse thrusting heat"))
		+ attributes.Get("turning heat")
		+ attributes.Get("afterburner heat");
	tableLabels.push_back("ship_info.moving");
	tableTooltipKeys.push_back("moving:");
	energyTable.push_back(Format::Number(-60. * movingEnergyPerFrame));
	heatTable.push_back(Format::Number(60. * movingHeatPerFrame));

	// Add energy and heat while firing to the table.
	attributesHeight += 20;
	double firingEnergy = 0.;
	double firingHeat = 0.;
	for(const auto &it : ship.Outfits())
	{
		const Weapon *weapon = it.first->GetWeapon().get();
		if(weapon && weapon->Reload())
		{
			firingEnergy += it.second * weapon->FiringEnergy() / weapon->Reload();
			firingHeat += it.second * weapon->FiringHeat() / weapon->Reload();
		}
	}
	tableLabels.push_back("ship_info.firing");
	tableTooltipKeys.push_back("firing:");
	energyTable.push_back(Format::Number(-60. * firingEnergy));
	heatTable.push_back(Format::Number(60. * firingHeat));

	// Add energy and heat when doing shield and hull repair to the table.
	attributesHeight += 20;
	double shieldEnergy = (hasShieldRegen) ? (attributes.Get("shield energy")
		+ attributes.Get("delayed shield energy"))
		* (1. + attributes.Get("shield energy multiplier")) : 0.;
	double hullEnergy = (hasHullRepair) ? (attributes.Get("hull energy")
		+ attributes.Get("delayed hull energy"))
		* (1. + attributes.Get("hull energy multiplier")) : 0.;
	string shieldHullKey = (shieldEnergy && hullEnergy) ? "ship_info.shields_hull" :
		hullEnergy ? "ship_info.repairing_hull" : "ship_info.charging_shields";
	tableLabels.push_back(shieldHullKey);
	tableTooltipKeys.push_back((shieldEnergy && hullEnergy) ? "shields / hull:" :
		hullEnergy ? "repairing hull:" : "charging shields:");
	energyTable.push_back(Format::Number(-60. * (shieldEnergy + hullEnergy)));
	double shieldHeat = (hasShieldRegen) ? (attributes.Get("shield heat")
		+ attributes.Get("delayed shield heat"))
		* (1. + attributes.Get("shield heat multiplier")) : 0.;
	double hullHeat = (hasHullRepair) ? (attributes.Get("hull heat")
		+ attributes.Get("delayed hull heat"))
		* (1. + attributes.Get("hull heat multiplier")) : 0.;
	heatTable.push_back(Format::Number(60. * (shieldHeat + hullHeat)));

	if(scrollingPanel)
	{
		attributesHeight += 20;
		const double overallEnergy = idleEnergyPerFrame
			- movingEnergyPerFrame
			- firingEnergy
			- shieldEnergy
			- hullEnergy;
		const double overallHeat = idleHeatPerFrame
			+ movingHeatPerFrame
			+ firingHeat
			+ shieldHeat
			+ hullHeat;
		tableLabels.push_back("ship_info.net_change");
		tableTooltipKeys.push_back("net change:");
		energyTable.push_back(Format::Number(60. * overallEnergy));
		heatTable.push_back(Format::Number(60. * overallHeat));
	}

	// Add maximum values of energy and heat to the table.
	attributesHeight += 20;
	const double maxEnergy = attributes.Get("energy capacity");
	const double maxHeat = 60. * ship.HeatDissipation() * ship.MaximumHeat();
	tableLabels.push_back("ship_info.max");
	tableTooltipKeys.push_back("max:");
	energyTable.push_back(Format::Number(maxEnergy));
	heatTable.push_back(Format::Number(maxHeat));
	// Pad by 10 pixels on the top and bottom.
	attributesHeight += 30;
}



void ShipInfoDisplay::UpdateOutfits(const Ship &ship, const PlayerInfo &player, const Depreciation &depreciation)
{
	outfitLabels.clear();
	outfitValues.clear();
	outfitTooltipKeys.clear();
	outfitsHeight = 20;

	map<string, map<string, int>> listing;
	for(const auto &it : ship.Outfits())
		if(it.first->IsDefined() && !it.first->Category().empty() && !it.first->DisplayName().empty())
			listing[it.first->Category()][it.first->TranslatedDisplayName()] += it.second;

	for(const auto &cit : listing)
	{
		if(&cit != &*listing.begin())
		{
			outfitLabels.push_back(string());
			outfitValues.push_back(string());
			outfitTooltipKeys.push_back(string());
			outfitsHeight += 10;
		}

		outfitLabels.push_back(Translation::TrCategory(cit.first) + ':');
		outfitTooltipKeys.push_back(cit.first + ':');
		outfitValues.push_back(string());
		outfitsHeight += 20;

		for(const auto &it : cit.second)
		{
			outfitLabels.push_back(it.first);
			outfitValues.push_back(to_string(it.second));
			outfitTooltipKeys.push_back(string());
			outfitsHeight += 20;
		}
	}


	int64_t totalCost = depreciation.Value(ship, player.GetDate().DaysSinceEpoch());
	int64_t chassisCost = depreciation.Value(
		GameData::Ships().Get(ship.TrueModelName()),
		player.GetDate().DaysSinceEpoch());
	saleLabels.clear();
	saleValues.clear();
	saleTooltipKeys.clear();
	saleHeight = 20;

	saleLabels.push_back(Translation::Tr("ship_info.sell_for"));
	saleTooltipKeys.push_back("This ship will sell for:");
	saleValues.push_back(string());
	saleHeight += 20;
	saleLabels.push_back(Translation::Tr("ship_info.empty_hull"));
	saleTooltipKeys.push_back("empty hull:");
	saleValues.push_back(Format::AbbreviatedNumber(chassisCost));
	saleHeight += 20;
	saleLabels.push_back(Translation::Tr("ship_info.plus_outfits"));
	saleTooltipKeys.push_back("  + outfits:");
	saleValues.push_back(Format::AbbreviatedNumber(totalCost - chassisCost));
	saleHeight += 20;
}
