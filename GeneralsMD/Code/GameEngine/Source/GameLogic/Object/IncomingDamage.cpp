/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// IncomingDamage.cpp /////////////////////////////////////////////////////////////////////////////
// The ledger of damage in flight. See IncomingDamage.h for what it is for.
///////////////////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file int the GameEngine

#include "GameLogic/IncomingDamage.h"

#include "GameLogic/Object.h"
#include "GameLogic/Module/BodyModule.h"

#include <map>
#include <vector>

//-------------------------------------------------------------------------------------------------
struct BookedShot
{
	ObjectID			m_shooter;			///< who fired it, so the landing damage can release its own booking
	Real					m_amount;				///< damage it is expected to take off the victim, after armor
	UnsignedInt		m_expireFrame;	///< when the booking lapses even though nothing landed
};

typedef std::vector<BookedShot> BookedShotVec;

// std::map, keyed by ObjectID: the sweep below walks it in ID order on every client.
typedef std::map<ObjectID, BookedShotVec> BookedShotMap;

static BookedShotMap theBookings;

//
// How long past the expected impact a booking is honored before it lapses.  A missile that is shot
// down or lured away by countermeasures never lands, and this is how long the target stays wrongly
// reserved: long enough to absorb the slop in the flight-time estimate below, short enough that the
// squad's pause is not noticeable.
//
static const UnsignedInt BOOKING_SLACK_FRAMES = 15;

// Flight time is estimated from the weapon speed, which for a guided projectile is only a hint --
// the projectile flies on its own locomotor. Clamp the estimate rather than trust it outright.
static const UnsignedInt MIN_FLIGHT_FRAMES = 1;
static const UnsignedInt MAX_FLIGHT_FRAMES = 90;

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::reset()
{
	theBookings.clear();
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::update(UnsignedInt currentFrame)
{
	for (BookedShotMap::iterator vic = theBookings.begin(); vic != theBookings.end(); )
	{
		BookedShotVec& shots = vic->second;
		for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); )
		{
			if (currentFrame >= s->m_expireFrame)
				s = shots.erase(s);
			else
				++s;
		}

		if (shots.empty())
		{
			// ObjectIDs are never reused, so an emptied entry is gone for good.
			BookedShotMap::iterator dead = vic;
			++vic;
			theBookings.erase(dead);
		}
		else
		{
			++vic;
		}
	}
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::bookShot(ObjectID victim, ObjectID shooter, Real amount,
																		 UnsignedInt currentFrame, UnsignedInt impactFrame)
{
	if (victim == INVALID_ID || amount <= 0.0f)
		return;

	UnsignedInt flight = (impactFrame > currentFrame) ? (impactFrame - currentFrame) : MIN_FLIGHT_FRAMES;
	if (flight < MIN_FLIGHT_FRAMES)
		flight = MIN_FLIGHT_FRAMES;
	else if (flight > MAX_FLIGHT_FRAMES)
		flight = MAX_FLIGHT_FRAMES;

	BookedShot shot;
	shot.m_shooter = shooter;
	shot.m_amount = amount;
	shot.m_expireFrame = currentFrame + flight + BOOKING_SLACK_FRAMES;

	theBookings[victim].push_back(shot);
}

//-------------------------------------------------------------------------------------------------
void IncomingDamageTracker::shotLanded(ObjectID victim, ObjectID shooter)
{
	BookedShotMap::iterator vic = theBookings.find(victim);
	if (vic == theBookings.end())
		return;

	BookedShotVec& shots = vic->second;
	for (BookedShotVec::iterator s = shots.begin(); s != shots.end(); ++s)
	{
		if (s->m_shooter == shooter)
		{
			// oldest first: a shooter's shots land in the order they were fired
			shots.erase(s);
			break;
		}
	}

	if (shots.empty())
		theBookings.erase(vic);
}

//-------------------------------------------------------------------------------------------------
Real IncomingDamageTracker::getBookedDamage(ObjectID victim)
{
	BookedShotMap::const_iterator vic = theBookings.find(victim);
	if (vic == theBookings.end())
		return 0.0f;

	Real total = 0.0f;
	const BookedShotVec& shots = vic->second;
	for (BookedShotVec::const_iterator s = shots.begin(); s != shots.end(); ++s)
		total += s->m_amount;

	return total;
}

//-------------------------------------------------------------------------------------------------
Bool IncomingDamageTracker::isAlreadyDoomed(ObjectID victim, Real remainingHealth)
{
	const Real booked = getBookedDamage(victim);
	if (booked <= 0.0f)
		return FALSE;

	return booked >= remainingHealth;
}

//-------------------------------------------------------------------------------------------------
Bool IncomingDamageTracker::isAlreadyDoomed(const Object *victim)
{
	return isAlreadyDoomed(victim->getID(), victim->getBodyModule()->getHealth());
}
