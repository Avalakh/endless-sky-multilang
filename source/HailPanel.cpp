/* HailPanel.cpp
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

#include "HailPanel.h"

#include "text/Alignment.h"
#include "audio/Audio.h"
#include "DialogPanel.h"
#include "shader/DrawList.h"
#include "text/Font.h"
#include "text/FontSet.h"
#include "text/Format.h"
#include "text/Translation.h"
#include "GameData.h"
#include "Government.h"
#include "Information.h"
#include "Interface.h"
#include "Messages.h"
#include "Planet.h"
#include "PlayerInfo.h"
#include "Politics.h"
#include "Ship.h"
#include "image/Sprite.h"
#include "StellarObject.h"
#include "System.h"
#include "UI.h"
#include "Weapon.h"
#include "text/WrappedText.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

using namespace std;



HailPanel::HailPanel(PlayerInfo &player, const shared_ptr<Ship> &ship, function<void(const Government *)> bribeCallback)
	: player(player), ship(ship), bribeCallback(std::move(bribeCallback)), facing(ship->Facing())
{
	Audio::Pause();
	SetInterruptible(false);
	UI::PlaySound(UI::UISound::SOFT);

	const Government *gov = ship->GetGovernment();
	if(!ship->GivenName().empty())
		header = Translation::TrGovernment(gov->DisplayName()) + " " + ship->Noun() + " \"" + ship->GivenName() + "\":";
	else
		header = ship->TranslatedDisplayModelName() + " (" + Translation::TrGovernment(gov->DisplayName()) + "):";
	// Drones are always unpiloted, so they never respond to hails.
	bool isMute = ship->GetPersonality().IsMute() || (ship->Attributes().Category() == "Drone");
	hasLanguage = !isMute && (gov->Language().empty() || player.Conditions().Get("language: " + gov->Language()));
	canAssistPlayer = !ship->CanBeCarried();

	if(isMute)
		SetMessage(Translation::Tr("hail.no_response"));
	else if(!hasLanguage)
		SetMessage(Translation::Tr("hail.alien_voice"));
	else if(gov->IsEnemy())
	{
		// Enemy ships always show hostile messages.
		// They either show bribing messages,
		// or standard hostile messages, if disabled.
		if(!ship->IsDisabled())
		{
			// If this government has a non-zero bribe threshold, it can only be bribed
			// if the player's reputation with them is not less than the threshold value.
			const double bribeThreshold = gov->GetBribeThreshold();
			if(!bribeThreshold || GameData::GetPolitics().Reputation(gov) >= bribeThreshold)
				SetBribe(gov->GetBribeFraction());
		}
	}
	else if(ship->IsDisabled())
	{
		const Ship *flagship = player.Flagship();
		if(flagship->NeedsFuel(false) || flagship->IsDisabled())
			SetMessage(Translation::Tr("hail.sorry_disabled"));
	}
	else
	{
		// Is the player in any need of assistance?
		const Ship *flagship = player.Flagship();
		// Check if the player is out of fuel or energy.
		if(flagship->NeedsFuel(false))
		{
			playerNeedsHelp = true;
			canGiveFuel = ship->CanRefuel(*flagship) && canAssistPlayer;
		}
		if(flagship->NeedsEnergy())
		{
			playerNeedsHelp = true;
			canGiveEnergy = ship->CanGiveEnergy(*flagship) && canAssistPlayer;
		}
		// Check if the player is disabled.
		if(flagship->IsDisabled())
		{
			playerNeedsHelp = true;
			canRepair = canAssistPlayer;
		}

		if(ship->GetPersonality().IsSurveillance())
		{
			canGiveFuel = false;
			canRepair = false;
		}

		if(ship->GetShipToAssist() == player.FlagshipPtr())
			SetMessage(Translation::Tr("hail.hang_on"));
		else if(canGiveFuel || canRepair)
		{
			string helpOffer = "Looks like you've gotten yourself into a bit of trouble. "
				"Would you like us to ";
			if(canGiveFuel && canRepair)
				helpOffer += "patch you up and give you some fuel?";
			else if(canGiveFuel)
				helpOffer += "give you some fuel?";
			else if(canRepair)
				helpOffer += "patch you up?";
			else if(canGiveEnergy)
				helpOffer += "recharge you?";
			SetMessage(helpOffer);
		}
		else if(playerNeedsHelp && !canAssistPlayer)
			SetMessage(Translation::Tr("hail.sorry_too_small"));
	}

	if(message.empty())
		SetMessage(ship->GetHail(player.GetSubstitutions()));
}



HailPanel::HailPanel(PlayerInfo &player, const StellarObject *object)
	: player(player), object(object), planet(object->GetPlanet()), facing(object->Facing())
{
	Audio::Pause();
	SetInterruptible(false);
	UI::PlaySound(UI::UISound::SOFT);

	const Government *gov = planet ? planet->GetGovernment() : player.GetSystem()->GetGovernment();
	if(planet)
		header = Translation::TrGovernment(gov->DisplayName()) + " " + planet->Noun() + " \"" + planet->DisplayName() + "\":";
	hasLanguage = (gov->Language().empty() || player.Conditions().Get("language: " + gov->Language()));

	// If the player is hailing a planet, determine if a mission grants them clearance before checking
	// if they have a language that matches the planet's government. This allows mission clearance
	// to bypass language barriers.
	if(planet && player.Flagship())
		for(const Mission &mission : player.Missions())
			if(mission.HasClearance(planet) && mission.ClearanceMessage() != "auto")
			{
				planet->Bribe(mission.HasFullClearance());
				SetMessage(mission.ClearanceMessage());
				return;
			}

	if(!hasLanguage)
		SetMessage("(An alien voice says something in a language you do not recognize.)");
	else if(planet && player.Flagship())
	{
		if(planet->CanLand())
			{
				map<string, string> rep = {{"name", player.Flagship()->GivenName()}};
				SetMessage(Translation::Tr("hail.cleared_to_land", rep));
			}
		else
		{
			if(planet->CanBribe())
				SetBribe(planet->GetBribeFraction());
			if(bribe)
				SetMessage(Translation::Tr("hail.land_cost")
					+ Format::CreditString(bribe) + ".");
			else if(gov->IsEnemy())
				SetMessage(Translation::Tr("hail.not_welcome"));
			else
				SetMessage(Translation::Tr("hail.cant_permit"));
		}
	}
}



HailPanel::~HailPanel()
{
	Audio::Resume();
}



void HailPanel::Draw()
{
	DrawBackdrop();

	Information info;
	info.SetString("header", header);
	if(ship)
	{
		info.SetCondition("show assist");
		if(hasLanguage && !ship->IsDisabled())
		{
			if(requestedToBribeShip)
				info.SetCondition("show pay bribe");
			if(ship->GetGovernment()->IsEnemy())
			{
				if(requestedToBribeShip)
					info.SetCondition("can pay bribe");
				else
					info.SetCondition("can bribe");
			}
			else if(!ship->CanBeCarried() && ship->GetShipToAssist() != player.FlagshipPtr())
				info.SetCondition("can assist");
		}
	}
	else
	{
		if(!GameData::GetPolitics().HasDominated(planet))
			info.SetCondition("show dominate");
		else
			info.SetCondition("show relinquish");
		if(hasLanguage)
		{
			info.SetCondition("can dominate");
			if(!planet->CanLand())
				info.SetCondition("can bribe");
		}
	}

	const Interface *hailUi = GameData::Interfaces().Get("hail panel");
	hailUi->Draw(info, this);

	const Sprite *sprite = ship ? ship->GetSprite() : object->GetSprite();

	// Draw the sprite, rotated, scaled, and swizzled as necessary.
	float zoom = min(2.f, 400.f / max(sprite->Width(), sprite->Height()));
	Point center(-170., -10.);

	DrawList draw;
	draw.Clear(step);
	// If this is a ship, copy its swizzle, animation settings, etc.
	// Also draw its fighters and weapon hardpoints.
	if(ship)
	{
		bool hasFighters = ship->PositionFighters();
		auto addHardpoint = [this, &draw, &center, zoom](const Hardpoint &hardpoint) -> void
		{
			const Weapon *weapon = hardpoint.GetWeapon();
			if(!weapon)
				return;
			const Body &sprite = weapon->HardpointSprite();
			if(!sprite.HasSprite())
				return;
			Body body(
				sprite,
				center + zoom * facing.Rotate(hardpoint.GetPoint()),
				Point(),
				facing + hardpoint.GetAngle(),
				zoom);
			if(body.InheritsParentSwizzle())
				body.SetSwizzle(ship->GetSwizzle());
			draw.Add(body);
		};
		auto addFighter = [this, &draw, &center, zoom](const Ship::Bay &bay) -> void
		{
			if(bay.ship)
			{
				Body body(
					*bay.ship,
					center + zoom * facing.Rotate(bay.point),
					Point(),
					facing + bay.facing,
					zoom);
				draw.Add(body);
			}
		};

		if(hasFighters)
			for(const Ship::Bay &bay : ship->Bays())
				if(bay.side == Ship::Bay::UNDER)
					addFighter(bay);
		for(const Hardpoint &hardpoint : ship->Weapons())
			if(hardpoint.GetSide() == Hardpoint::Side::UNDER)
				addHardpoint(hardpoint);
		draw.Add(Body(*ship, center, Point(), facing, zoom));
		for(const Hardpoint &hardpoint : ship->Weapons())
			if(hardpoint.GetSide() == Hardpoint::Side::OVER)
				addHardpoint(hardpoint);
		if(hasFighters)
			for(const Ship::Bay &bay : ship->Bays())
				if(bay.side == Ship::Bay::OVER)
					addFighter(bay);
	}
	else
		draw.Add(Body(*object, center, Point(), facing, zoom));

	draw.Draw();

	// Draw the current message.
	WrappedText wrap;
	wrap.SetAlignment(Alignment::JUSTIFIED);
	wrap.SetWrapWidth(330);
	wrap.SetFont(FontSet::Get(14));
	wrap.Wrap(message);
	wrap.Draw(Point(-50., -50.), *GameData::Colors().Get("medium"));

	++step;
}



bool HailPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	UI::UISound sound = UI::UISound::NORMAL;
	bool shipIsEnemy = (ship && ship->GetGovernment()->IsEnemy());
	const Government *gov = ship ? ship->GetGovernment() : planet ? planet->GetGovernment() : nullptr;

	if(key == 'd' || key == SDLK_ESCAPE || key == SDLK_RETURN || (key == 'w' && (mod & (KMOD_CTRL | KMOD_GUI))))
	{
		if(bribeCallback && bribed)
			bribeCallback(bribed);
		GetUI().Pop(this);
		sound = UI::UISound::SOFT;
	}
	else if(key == 't' && hasLanguage && planet)
	{
		if(GameData::GetPolitics().HasDominated(planet))
		{
			GameData::GetPolitics().DominatePlanet(planet, false);
			// Set payment 0 to erase the tribute.
			player.SetTribute(planet, 0);
			SetMessage(Translation::Tr("hail.thank_freedom"));
		}
		else if(!planet->IsDefending())
			GetUI().Push(DialogPanel::CallFunctionIfOk([this]() { SetMessage(planet->DemandTribute(player)); },
				Translation::Tr("hail.demand_tribute_warning"), false));
		else
			SetMessage(planet->DemandTribute(player));
		UI::PlaySound(UI::UISound::NORMAL);
		return true;
	}
	else if(key == 'h' && hasLanguage && ship && canAssistPlayer)
	{
		if(shipIsEnemy || ship->IsDisabled())
			return false;
		if(playerNeedsHelp)
		{
			if(ship->GetPersonality().IsSurveillance())
				SetMessage(Translation::Tr("hail.sorry_busy"));
			else if(canGiveFuel || canRepair || canGiveEnergy)
			{
				ship->SetShipToAssist(player.FlagshipPtr());
				SetMessage(Translation::Tr("hail.hang_on"));
			}
			else if(player.Flagship()->NeedsFuel(false))
			{
				if(ship->Fuel())
					SetMessage(Translation::Tr("hail.sorry_fuel_insufficient"));
				else
					SetMessage(Translation::Tr("hail.sorry_no_fuel"));
			}
			else if(player.Flagship()->NeedsEnergy())
			{
				if(ship->Energy())
					SetMessage(Translation::Tr("hail.sorry_energy_insufficient"));
				else
					SetMessage(Translation::Tr("hail.sorry_no_energy"));
			}
			else // shouldn't happen
				SetMessage(Translation::Tr("hail.sorry_unable_assist"));
		}
		else
		{
			if(bribe)
				SetMessage(Translation::Tr("hail.dont_push_luck"));
			else
				SetMessage(Translation::Tr("hail.no_need_assist"));
		}
	}
	else if((key == 'b' || key == 'o') && hasLanguage)
	{
		if(!gov)
			return true;

		// Make sure it actually makes sense to bribe this ship.
		if((ship && !shipIsEnemy) || (planet && planet->CanLand()))
			return true;

		if(bribe > player.Accounts().Credits())
			SetMessage(Translation::Tr("hail.not_worth_while"));
		else if(bribe)
		{
			if(!ship || requestedToBribeShip)
			{
				player.Accounts().AddCredits(-bribe);
				if(planet)
					SetMessage(gov->GetPlanetBribeAcceptanceHail());
				else
					SetMessage(gov->GetShipBribeAcceptanceHail());
			}
			if(ship)
			{
				if(!requestedToBribeShip)
				{
					SetMessage(Translation::Tr("hail.leave_alone_cost")
						+ Format::CreditString(bribe) + ".");
					requestedToBribeShip = true;
				}
				else
				{
					bribed = ship->GetGovernment();
					bribed->Bribe();
					{
						map<string, string> rep = {{"gov", Translation::TrGovernment(bribed->DisplayName())}, {"credits", Format::CreditString(bribe)}};
						Messages::Add({Translation::Tr("message.bribed_ship", rep),
							GameData::MessageCategories().Get("normal")});
					}
				}
			}
			else
			{
				planet->Bribe();
				{
					map<string, string> rep = {{"planet", planet->DisplayName()}, {"credits", Format::CreditString(bribe)}};
					Messages::Add({Translation::Tr("message.bribed_authorities", rep),
						GameData::MessageCategories().Get("normal")});
				}
			}
		}
		else if(planet)
			SetMessage(gov->GetPlanetBribeRejectionHail());
		else
			SetMessage(gov->GetShipBribeRejectionHail());
	}
	else
		sound = UI::UISound::NONE;

	UI::PlaySound(sound);
	return true;
}



void HailPanel::SetBribe(double scale)
{
	// Find the total value of your fleet.
	int64_t value = 0;
	for(const shared_ptr<Ship> &it : player.Ships())
		value += it->Cost();

	if(value <= 0)
		value = 1;

	bribe = 1000 * static_cast<int64_t>(sqrt(value) * scale);
}



void HailPanel::SetMessage(const string &text)
{
	message = text;
	if(!message.empty())
		Messages::Add({Translation::Tr("message.response_to_hail") + header + " " + message,
			GameData::MessageCategories().Get("log only")});
}
