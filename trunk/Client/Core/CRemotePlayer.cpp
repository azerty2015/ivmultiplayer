//============== IV: Multiplayer - http://code.iv-multiplayer.com ==============
//
// File: CRemotePlayer.cpp
// Project: Client.Core
// Author(s): jenksta
//            TrojaA
//            mabako
// License: See LICENSE in root directory
//
//==============================================================================

#include "CRemotePlayer.h"
#include "CChatWindow.h"
#include "CVehicleManager.h"
#include "CPlayerManager.h"
#include "Scripting.h"
#include "CLocalPlayer.h"
#include "CGame.h"

extern CChatWindow * g_pChatWindow;
extern CVehicleManager * g_pVehicleManager;
extern CLocalPlayer * g_pLocalPlayer;

CRemotePlayer::CRemotePlayer()
{
	m_vehicleId = INVALID_ENTITY_ID;
	m_uiBlipId = NULL;
	m_stateType = STATE_TYPE_DISCONNECT;
	m_bPassenger = false;
}

CRemotePlayer::~CRemotePlayer()
{
	Destroy();
}

bool CRemotePlayer::Spawn(CVector3 vecSpawnPos, float fSpawnHeading, bool bDontRecreate)
{
	if(!bDontRecreate)
	{
		if(IsSpawned())
			return false;

		if(!Create())
			return false;
	}

	Teleport(vecSpawnPos);
	SetCurrentHeading(fSpawnHeading);
	Init();
	return true;
}

void CRemotePlayer::Destroy()
{
	//CNetworkPlayer::Destroy();

	// remove the blip
	if(m_uiBlipId != NULL)
	{
		Scripting::RemoveBlip(m_uiBlipId);
		m_uiBlipId = NULL;
	}
}

void CRemotePlayer::Kill()
{
	CNetworkPlayer::Kill();

	if(IsSpawned())
		m_stateType = STATE_TYPE_DEATH;
}

void CRemotePlayer::Init()
{
	if(IsSpawned())
	{
		Scripting::SetDontActivateRagdollFromPlayerImpact(GetScriptingHandle(), true);
		Scripting::AddBlipForChar(GetScriptingHandle(), &m_uiBlipId);
		Scripting::ChangeBlipSprite(m_uiBlipId, Scripting::BLIP_OBJECTIVE);
		Scripting::ChangeBlipScale(m_uiBlipId, 0.5);
		Scripting::ChangeBlipNameFromAscii(m_uiBlipId, GetName());
		ToggleRagdoll(false);
		SetColor(GetColor());
		Scripting::SetPedDiesWhenInjured(GetScriptingHandle(), false);
		Scripting::SetCharInvincible(GetScriptingHandle(), true);

		if(!CGame::GetNameTags())
			Scripting::GivePedFakeNetworkName(GetScriptingHandle(),GetName().Get(),GetColor(),GetColor(),GetColor(),GetColor());

		// These two will be useful for a setPlayerUseModelAnims native
		//Scripting::SetAnimGroupForChar(m_pedIndex, "move_player");
		//Scripting::SetCharGestureGroup(m_pedIndex, "GESTURES@MALE");
		Scripting::BlockCharHeadIk(GetScriptingHandle(), true);
		m_stateType = STATE_TYPE_SPAWN;
	}
}

void CRemotePlayer::StoreOnFootSync(OnFootSyncData * syncPacket)
{
	// If we are in a vehicle remove ourselves from it
	if(IsInVehicle())
		RemoveFromVehicle();

	// Set our control state
	SetControlState(&syncPacket->controlState);

	// Set our position
	SetTargetPosition(syncPacket->vecPos, TICK_RATE);

	// Set our heading
	SetCurrentHeading(syncPacket->fHeading);

	// Set our move speed
	SetMoveSpeed(syncPacket->vecMoveSpeed);

	// Set our ducking state
	SetDucking(syncPacket->bDuckState);

	// Lock our health
	LockHealth(syncPacket->uHealthArmour >> 16);

	// Lock our armour
	LockArmour((syncPacket->uHealthArmour << 16) >> 16);

	// Set our anim stuff
	//bool banim = syncPacket->bAnim;
	//float fdensity = syncPacket->fAnimTime;
	//if(banim)
	//{
	//	m_strAnimGroup = syncPacket->szAnimGroup;
	//	m_strAnimSpec = syncPacket->szAnimSpecific;
	//	const char *szGroup = m_strAnimGroup;
	//	const char *szAnim = m_strAnimSpec;
	//	if(!m_bAnimating)
	//	{
	//		m_bAnimating = true;
	//		Scripting::TaskPlayAnim(GetScriptingHandle(),szAnim,szGroup,8.0f,0,0,0,0,-1);
	//		Scripting::SetCharAnimCurrentTime(GetScriptingHandle(),szGroup,szAnim,fdensity);
	//	}
	//	else if(m_bAnimating)
	//		Scripting::SetCharAnimCurrentTime(GetScriptingHandle(),szGroup,szAnim,fdensity);
	//}
	//else
	//	m_bAnimating = false;

	// Get our new weapon and ammo
	unsigned int uiWeapon = (syncPacket->uWeaponInfo >> 20);
	unsigned int uiAmmo = ((syncPacket->uWeaponInfo << 12) >> 12);

	// Do we not have the right weapon equipped?
	if(GetCurrentWeapon() != uiWeapon)
	{
		// Set our current weapon
		GiveWeapon(uiWeapon, uiAmmo);
	}

	// Do we not have the right ammo?
	if(GetAmmo(uiWeapon) != uiAmmo)
	{
		// Set our ammo
		SetAmmo(uiWeapon, uiAmmo);
	}

	m_stateType = STATE_TYPE_ONFOOT;
}

void CRemotePlayer::StoreInVehicleSync(EntityId vehicleId, InVehicleSyncData * syncPacket)
{
	// Get the vehicle
	CNetworkVehicle * pVehicle = g_pVehicleManager->Get(vehicleId);

	// Does the vehicle exist and is it spawned?
	if(pVehicle && pVehicle->IsStreamedIn())
	{
		// Store the vehicle id
		m_vehicleId = vehicleId;

		// If they have just warped into the vehicle and the local player
		// is driving it eject the local player
		if(m_stateType != STATE_TYPE_INVEHICLE && pVehicle->GetDriver() == g_pLocalPlayer)
		{
			g_pLocalPlayer->RemoveFromVehicle();
			g_pChatWindow->AddInfoMessage("Car Jacked");
		}

		// If they are not in the vehicle put them in it
		if(GetVehicle() != pVehicle)
		{
			if(g_pLocalPlayer->GetVehicle() == pVehicle && !g_pLocalPlayer->IsAPassenger())
				g_pLocalPlayer->RemoveFromVehicle();

			PutInVehicle(pVehicle, 0);
		}

		// Set their control state
		SetControlState(&syncPacket->controlState);

		// Set their vehicles target position
		pVehicle->SetTargetPosition(syncPacket->vecPos, TICK_RATE);

		// Set their vehicles target rotation
		pVehicle->SetTargetRotation(syncPacket->vecRotation, TICK_RATE);
		
		// Set their vehicles turn speed
		pVehicle->SetTurnSpeed(syncPacket->vecTurnSpeed);

		// Set their vehicles move speed
		pVehicle->SetMoveSpeed(syncPacket->vecMoveSpeed);

		// Set their vehicles health
		pVehicle->SetHealth(syncPacket->uiHealth);
		pVehicle->SetPetrolTankHealth(syncPacket->fPetrolHealth);

		// Set their door states
		pVehicle->ControlCar(0,1,syncPacket->fDoor[0]);
		pVehicle->ControlCar(1,1,syncPacket->fDoor[1]);
		pVehicle->ControlCar(2,1,syncPacket->fDoor[2]);
		pVehicle->ControlCar(3,1,syncPacket->fDoor[3]);
		pVehicle->ControlCar(4,1,syncPacket->fDoor[4]);
		pVehicle->ControlCar(5,1,syncPacket->fDoor[5]);

		// Set their vehicles color
		pVehicle->SetColors(syncPacket->byteColors[0], syncPacket->byteColors[1], syncPacket->byteColors[2], syncPacket->byteColors[3]);

		// Set their vehicles siren state
		pVehicle->SetSirenState(syncPacket->bSirenState);

		// Set their windows
		pVehicle->SetWindow(0, syncPacket->bWindow[0]);
		pVehicle->SetWindow(1, syncPacket->bWindow[1]);
		pVehicle->SetWindow(2, syncPacket->bWindow[2]);
		pVehicle->SetWindow(3, syncPacket->bWindow[3]);

		// Set their typres
		if(syncPacket->bTyre[0])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)0);
		if(syncPacket->bTyre[1])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)1);
		if(syncPacket->bTyre[2])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)2);
		if(syncPacket->bTyre[3])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)3);
		if(syncPacket->bTyre[4])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)4);
		if(syncPacket->bTyre[5])
			Scripting::BurstCarTyre(GetScriptingHandle(),(Scripting::eVehicleTyre)5);

		// Set their vehicles dirt level
		pVehicle->SetDirtLevel(syncPacket->fDirtLevel);

		// Set their vehicles engine status
		pVehicle->SetEngineState(syncPacket->bEngineStatus);

		// Set their lights
		pVehicle->SetLights(syncPacket->bLights);

		// Lock our health
		LockHealth(syncPacket->uPlayerHealthArmour >> 16);

		// Lock our armour
		LockArmour((syncPacket->uPlayerHealthArmour << 16) >> 16);

		// Get our new weapon and ammo
		unsigned int uiWeapon = (syncPacket->uPlayerWeaponInfo >> 20);
		unsigned int uiAmmo = ((syncPacket->uPlayerWeaponInfo << 12) >> 12);

		// Do we not have the right weapon equipped?
		if(GetCurrentWeapon() != uiWeapon)
		{
			// Set our current weapon
			GiveWeapon(uiWeapon, uiAmmo);
		}

		// Do we not have the right ammo?
		if(GetAmmo(uiWeapon) != uiAmmo)
		{
			// Set our ammo
			SetAmmo(uiWeapon, uiAmmo);
		}

	}
	else if(pVehicle && !(pVehicle->IsStreamedIn()))
	{
		//set the vehicle position
		pVehicle->SetPosition(syncPacket->vecPos, false, true);

		//set the player position
		SetPosition(syncPacket->vecPos, true);
	}

	m_stateType = STATE_TYPE_INVEHICLE;
}

void CRemotePlayer::StorePassengerSync(EntityId vehicleId, PassengerSyncData * syncPacket)
{
	// Get the vehicle
	CNetworkVehicle * pVehicle = g_pVehicleManager->Get(vehicleId);

	// Does the vehicle exist and is it spawned?
	if(pVehicle && pVehicle->IsStreamedIn())
	{
		// Store the vehicle id
		m_vehicleId = vehicleId;

		// If they are not in the vehicle put them in it
		if(GetVehicle() != pVehicle || GetVehicleSeatId() != syncPacket->byteSeatId)
			PutInVehicle(pVehicle, syncPacket->byteSeatId);

		// Set their control state
		SetControlState(&syncPacket->controlState);

		// Lock our health
		LockHealth(syncPacket->uPlayerHealthArmour >> 16);

		// Lock our armour
		LockArmour((syncPacket->uPlayerHealthArmour << 16) >> 16);

		// Get our new weapon and ammo
		unsigned int uiWeapon = (syncPacket->uPlayerWeaponInfo >> 20);
		unsigned int uiAmmo = ((syncPacket->uPlayerWeaponInfo << 12) >> 12);

		// Do we not have the right weapon equipped?
		if(GetCurrentWeapon() != uiWeapon)
		{
			// Set our current weapon
			GiveWeapon(uiWeapon, uiAmmo);
		}

		// Do we not have the right ammo?
		if(GetAmmo(uiWeapon) != uiAmmo)
		{
			// Set our ammo
			SetAmmo(uiWeapon, uiAmmo);
		}
	}

	m_stateType = STATE_TYPE_PASSENGER;
}

void CRemotePlayer::StoreSmallSync(SmallSyncData * syncPacket)
{
	// Set their control state
	SetControlState(&syncPacket->controlState);

	// Set their ducking state
	SetDucking(syncPacket->bDuckState);

	// Get our new weapon and ammo
	unsigned int uiWeapon = (syncPacket->uWeaponInfo >> 20);
	unsigned int uiAmmo = ((syncPacket->uWeaponInfo << 12) >> 12);

	// Do we not have the right weapon equipped?
	if(GetCurrentWeapon() != uiWeapon)
	{
		// Set our current weapon
		GiveWeapon(uiWeapon, uiAmmo);
	}

	// Do we not have the right ammo?
	if(GetAmmo(uiWeapon) != uiAmmo)
	{
		// Set our ammo
		SetAmmo(uiWeapon, uiAmmo);
	}
}

void CRemotePlayer::SetColor(unsigned int uiColor)
{
	CNetworkPlayer::SetColor(uiColor);

	if(!CGame::GetNameTags())
	{
		Scripting::RemoveFakeNetworkNameFromPed(GetScriptingHandle());
		Scripting::GivePedFakeNetworkName(GetScriptingHandle(),GetName().Get(),uiColor,uiColor,uiColor,uiColor);
	}

	if(m_uiBlipId != NULL)
		Scripting::ChangeBlipColour(m_uiBlipId, uiColor);

}